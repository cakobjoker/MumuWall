#include <Arduino.h>

// Include necessary libraries
#include <Adafruit_GFX.h>
#include <FastLED_NeoMatrix.h>
#include <FastLED.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>


// Define constants for the LED matrix
#define PIN 21
#define BRIGHTNESS 24      // Brightness of the LED matrix (out of 255)
#define PANEL_WIDTH 16
#define PANEL_HEIGHT 16
#define PANELS_WIDE 3      // 5 panels wide
#define PANELS_HIGH 3      // 8 panels high (5×8 = 40 total)
#define NUM_LEDS (PANEL_WIDTH * PANEL_HEIGHT * PANELS_WIDE * PANELS_HIGH)

#define MAX_PANELS 12
#define DEBUG 0  // Disable serial debug output to avoid interfering with LMCSHD
#define DEBUG_LED 2  // Onboard LED for visual feedback (GPIO 2 on most ESP32)

// debug toggle: set to 1 to enable serial debug, 0 to disable
//#define DEBUG 1

// #define onboard 2

static int width = PANEL_WIDTH * PANELS_WIDE;   // Total matrix width
static int height = PANEL_HEIGHT * PANELS_HIGH; // Total matrix height
int NUM_PANELS = (PANELS_WIDE * PANELS_HIGH); // Total number of panels (40)
bool dataReceived = false; // Track if we've received frame data

// Streaming buffer for non-blocking reads
uint8_t streamBuffer[NUM_LEDS * 3]; // Max 24BPP frame
size_t streamPos = 0;
size_t streamExpect = 0;

// Array to hold the LED color data
CRGB ledArray[NUM_LEDS];

// Create a new NeoMatrix object
FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(ledArray, 16, 16, 2, 2, 
  NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG + 
  NEO_TILE_BOTTOM + NEO_TILE_LEFT +  NEO_TILE_COLUMNS + NEO_TILE_ZIGZAG);

// Arrays to hold the draw data and pass data
uint16_t DrawData[NUM_LEDS]; 
// Support up to 24BPP (3 bytes) for up to MAX_PANELS panels
uint8_t PassData[NUM_LEDS * 3 * MAX_PANELS]; 
uint8_t * RawData8 = NULL;
uint16_t * RawData16 = NULL;
uint8_t * RawData24 = NULL;

// Forward declarations
void DrawTheFrame8();
void DrawTheFrame16();
void DrawTheFrame24();
void GetTheData8();
void GetTheData16();
void GetTheData24();
void SerialEvent();
void handleCommand(uint8_t cmd);
void processStreamFrame();

// New helper: identify bytes that are protocol commands
static inline bool isCommandByte(uint8_t b) {
	// list of command header bytes used by the protocol
	if (b == 0x05 || b == 0x42 || b == 0x43 || b == 0x44 || b == 0xFF) return true;
	if (b >= 0xC0 && b <= 0xC4) return true; // 0xC1-0xC4 family
	if (b >= 0x80 && b <= 0x83) return true; // 0x81-0x83 family
	return false;
}

// Visual feedback helpers with longer pauses between patterns
void blinkLED(int times, int delayMs = 100) {
  for (int i = 0; i < times; i++) {
    digitalWrite(DEBUG_LED, HIGH);
    delay(delayMs);
    digitalWrite(DEBUG_LED, LOW);
    delay(delayMs);
  }
  delay(500); // Pause between blink patterns to distinguish them
}

void setup() {
  // Setup debug LED
  pinMode(DEBUG_LED, OUTPUT);
  digitalWrite(DEBUG_LED, LOW);
  
  // Quick startup blink without delay blocking
  for (int i = 0; i < 3; i++) {
    digitalWrite(DEBUG_LED, HIGH);
    delayMicroseconds(50000);
    digitalWrite(DEBUG_LED, LOW);
    delayMicroseconds(50000);
  }
  
  // Add LEDs to the FastLED library
  FastLED.addLeds<WS2812B, PIN, GRB>(ledArray, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  
  // Initialize serial communication
  Serial.begin(115200);
  Serial.setTimeout(0); // Non-blocking mode
}

void loop(){
  // Non-blocking serial read loop
  while (Serial.available()) {
    if (streamExpect == 0) {
      // Reading command byte
      uint8_t cmd = Serial.read();
      handleCommand(cmd);
    } else {
      // Reading frame data
      uint8_t byte = Serial.read();
      streamBuffer[streamPos++] = byte;
      
      if (streamPos >= streamExpect) {
        // Frame complete
        processStreamFrame();
        streamPos = 0;
        streamExpect = 0;
      }
    }
  }
}

void handleCommand(uint8_t cmd) {
  switch (cmd) {
  case 0x05:
    Serial.print(width);
    Serial.print(' ');
    Serial.println(height);
    Serial.flush();
    break;
  
  case 0x42:
    // Expect 16-bit frame
    streamExpect = (size_t)NUM_LEDS * 2;
    streamPos = 0;
    break;
  
  case 0x43:
    // Expect 8-bit frame
    streamExpect = (size_t)NUM_LEDS;
    streamPos = 0;
    break;

  case 0x44:
    // Expect 24-bit frame
    streamExpect = (size_t)NUM_LEDS * 3;
    streamPos = 0;
    break;

  case 0xC1:
  case 0xC2:
  case 0xC3:
    if (Serial.available()) {
      uint8_t panelByte = Serial.read();
      int requested = (panelByte == 0x0F) ? (PANELS_WIDE * PANELS_HIGH) : (panelByte & 0x0F);
      if (requested == 0) requested = 1;
      NUM_PANELS = (requested > MAX_PANELS) ? MAX_PANELS : requested;
      streamExpect = (size_t)NUM_LEDS * NUM_PANELS * 2;
      streamPos = 0;
    }
    break;

  case 0x81:
  case 0x82:
  case 0x83:
    if (Serial.available()) {
      uint8_t panelByte = Serial.read();
      int requested = (panelByte == 0x0F) ? (PANELS_WIDE * PANELS_HIGH) : (panelByte & 0x0F);
      if (requested == 0) requested = 1;
      NUM_PANELS = (requested > MAX_PANELS) ? MAX_PANELS : requested;
      streamExpect = (size_t)NUM_LEDS * NUM_PANELS;
      streamPos = 0;
    }
    break;

  case 0xC4:
    if (Serial.available()) {
      uint8_t panelByte = Serial.read();
      int requested = (panelByte == 0x0F) ? (PANELS_WIDE * PANELS_HIGH) : (panelByte & 0x0F);
      if (requested == 0) requested = 1;
      NUM_PANELS = (requested > MAX_PANELS) ? MAX_PANELS : requested;
      streamExpect = (size_t)NUM_LEDS * NUM_PANELS * 3;
      streamPos = 0;
    }
    break;
    
  case 0xFF:
    if (Serial.available() && Serial.peek() == 0xFE) {
      Serial.read();
      for (int i = 0; i < NUM_LEDS; i++) {
        ledArray[i] = CRGB(255, 255, 255);
      }
      FastLED.show();
    }
    break;

  default:
    break;
  }
}

void processStreamFrame() {
  // Determine frame type based on streamExpect
  if (streamExpect == (size_t)NUM_LEDS * 2) {
    // 16-bit RGB565
    memcpy(PassData, streamBuffer, streamExpect);
    DrawTheFrame16();
  } else if (streamExpect == (size_t)NUM_LEDS) {
    // 8-bit RGB332
    memcpy(PassData, streamBuffer, streamExpect);
    DrawTheFrame8();
  } else if (streamExpect == (size_t)NUM_LEDS * 3) {
    // 24-bit RGB888
    memcpy(PassData, streamBuffer, streamExpect);
    DrawTheFrame24();
  }
  
  Serial.write(0x06); // Send ack
  dataReceived = true;
}

void GetTheData8(){
  size_t expect = (size_t)NUM_LEDS * NUM_PANELS;
  if (Serial.readBytes(PassData, expect) != (int)expect) {
    // Timeout error: 3 quick pulses
    blinkLED(3, 80);
    dataReceived = false; // Mark data as invalid
    return;
  }
  DrawTheFrame8(); 
  Serial.write(0x06); 
}

void GetTheData16(){
  size_t expect = (size_t)NUM_LEDS * NUM_PANELS * 2;
  if (Serial.readBytes(PassData, expect) != (int)expect) {
    blinkLED(3, 80);
    dataReceived = false;
    return;
  }
  DrawTheFrame16(); 
  Serial.write(0x06); 
}

void GetTheData24(){
  size_t expect = (size_t)NUM_LEDS * NUM_PANELS * 3;
  if (Serial.readBytes(PassData, expect) != (int)expect) {
    blinkLED(3, 80);
    dataReceived = false;
    return;
  }
  DrawTheFrame24(); 
  Serial.write(0x06); 
}

void DrawTheFrame16(){
  // Use the start of PassData (local panel data) instead of offset used for forwarding
  RawData16 = (uint16_t *) &PassData[0];

  // Convert each 16-bit RGB565 pixel into 8-bit RGB and write into ledArray
  for (int i = 0; i < NUM_LEDS; i++) {
    uint16_t pix = RawData16[i];
    // swap endian if necessary (original code did swap)
    pix = ((pix & 0xFF) << 8) | ((pix & 0xFF00) >> 8);
    // Extract RGB565 components
    uint8_t r5 = (pix >> 11) & 0x1F;
    uint8_t g6 = (pix >> 5) & 0x3F;
    uint8_t b5 = pix & 0x1F;
    // Expand to 8-bit per channel
    uint8_t r8 = (r5 << 3) | (r5 >> 2);
    uint8_t g8 = (g6 << 2) | (g6 >> 4);
    uint8_t b8 = (b5 << 3) | (b5 >> 2);
    ledArray[i] = CRGB(r8, g8, b8);
  }

  // Show the LEDs
  FastLED.show();
}

void DrawTheFrame8(){
  // Use the start of PassData (local panel data)
  RawData8 = &PassData[0];
  // Map the 8-bit color (RGB332-like) to 8-bit RGB and write into ledArray
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t v = RawData8[i];
    uint8_t r3 = (v & 0xE0) >> 5;
    uint8_t g3 = (v & 0x1C) >> 2;
    uint8_t b2 = (v & 0x03);
    // Expand to 8-bit (simple scaling)
    uint8_t r8 = (r3 << 5) | (r3 << 2) | (r3 >> 1);
    uint8_t g8 = (g3 << 5) | (g3 << 2) | (g3 >> 1);
    uint8_t b8 = (b2 << 6) | (b2 << 4) | (b2 << 2) | b2;
    ledArray[i] = CRGB(r8, g8, b8);
  }

  // Show the LEDs
  FastLED.show();
}

void DrawTheFrame24(){
  // Use the start of PassData (local panel data)
  RawData24 = &PassData[0];
  // Map the 24-bit color (RGB888) to ledArray
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t r8 = RawData24[i * 3];
    uint8_t g8 = RawData24[i * 3 + 1];
    uint8_t b8 = RawData24[i * 3 + 2];
    ledArray[i] = CRGB(r8, g8, b8);
  }

  // Show the LEDs
  FastLED.show();
}