#include <Arduino.h>

// Include necessary libraries
#include <Adafruit_GFX.h>
#include <FastLED_NeoMatrix.h>
#include <FastLED.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>


// Define constants for the LED matrix
#define PIN 21
#define BRIGHTNESS 36      // Brightness of the LED matrix (out of 255)

// Individual matrix size (each 16×16 matrix module)
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 16

// Configuration: number of matrices in each direction
#define MATRICES_WIDE 4     // 3 matrices wide
#define MATRICES_HIGH 3     // 3 matrices high

// Total panel dimensions (configuration width/height)
#define PANEL_WIDTH (MATRIX_WIDTH * MATRICES_WIDE)    // 48
#define PANEL_HEIGHT (MATRIX_HEIGHT * MATRICES_HIGH)  // 48

// Total LEDs in this panel configuration
#define NUM_LEDS (PANEL_WIDTH * PANEL_HEIGHT)

// Number of microcontroller units (panels)
#define NUM_PANELS 1  // Currently 1 microcontroller + matrix group

#define DEBUG 0  // Disable serial debug output to avoid interfering with LMCSHD
#define DEBUG_LED 2  // Onboard LED for visual feedback (GPIO 2 on most ESP32)

static int width = PANEL_WIDTH;      // Total matrix width
static int height = PANEL_HEIGHT;    // Total matrix height
bool dataReceived = false; // Track if we've received frame data

// Streaming buffer for non-blocking reads
uint8_t streamBuffer[NUM_LEDS * 3]; // Max 24BPP frame
size_t streamPos = 0;
size_t streamExpect = 0;
int panelsToRead = 0;  // Track how many panels are being read

// Array to hold the LED color data
CRGB ledArray[NUM_LEDS];

// Create a new NeoMatrix object
FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(ledArray, MATRIX_WIDTH, MATRIX_HEIGHT, MATRICES_WIDE, MATRICES_HIGH, 
  NEO_MATRIX_TOP     + NEO_MATRIX_RIGHT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG + 
  NEO_TILE_TOP + NEO_TILE_LEFT +  NEO_TILE_COLUMNS + NEO_TILE_ZIGZAG);

// Arrays to hold the draw data and pass data
uint16_t DrawData[NUM_LEDS]; 
uint8_t PassData[NUM_LEDS * 3]; // Single panel frame buffer
uint8_t * RawData8 = NULL;
uint16_t * RawData16 = NULL;
uint8_t * RawData24 = NULL;

// Forward declarations
void DrawTheFrame8FromBuffer(uint8_t* data);
void DrawTheFrame16FromBuffer(uint16_t* data);
void DrawTheFrame24FromBuffer(uint8_t* data);
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
  Serial.begin(250000);
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
    // 16-bit multi-panel command (for future support)
    if (Serial.available()) {
      Serial.read(); // Consume panel byte (currently ignored)
      streamExpect = (size_t)NUM_LEDS * 2;
      streamPos = 0;
    }
    break;

  case 0x81:
  case 0x82:
  case 0x83:
    // 8-bit multi-panel command (for future support)
    if (Serial.available()) {
      Serial.read(); // Consume panel byte (currently ignored)
      streamExpect = (size_t)NUM_LEDS;
      streamPos = 0;
    }
    break;

  case 0xC4:
    // 24-bit multi-panel command (for future support)
    if (Serial.available()) {
      Serial.read(); // Consume panel byte (currently ignored)
      streamExpect = (size_t)NUM_LEDS * 3;
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
  if (streamExpect == (size_t)NUM_LEDS * 2) {
    DrawTheFrame16FromBuffer((uint16_t*)streamBuffer);
  } else if (streamExpect == (size_t)NUM_LEDS) {
    DrawTheFrame8FromBuffer(streamBuffer);
  } else if (streamExpect == (size_t)NUM_LEDS * 3) {
    DrawTheFrame24FromBuffer(streamBuffer);
  }
  
  Serial.write(0x06);
  dataReceived = true;
}

void GetTheData8(){
  size_t expect = (size_t)NUM_LEDS;
  if (Serial.readBytes(PassData, expect) != (int)expect) {
    blinkLED(3, 80);
    dataReceived = false;
    return;
  }
  DrawTheFrame8FromBuffer(PassData); 
  Serial.write(0x06); 
}

void GetTheData16(){
  size_t expect = (size_t)NUM_LEDS * 2;
  if (Serial.readBytes(PassData, expect) != (int)expect) {
    blinkLED(3, 80);
    dataReceived = false;
    return;
  }
  DrawTheFrame16FromBuffer((uint16_t*)PassData); 
  Serial.write(0x06); 
}

void GetTheData24(){
  size_t expect = (size_t)NUM_LEDS * 3;
  if (Serial.readBytes(PassData, expect) != (int)expect) {
    blinkLED(3, 80);
    dataReceived = false;
    return;
  }
  DrawTheFrame24FromBuffer(PassData); 
  Serial.write(0x06); 
}

void DrawTheFrame16FromBuffer(uint16_t* data){
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    int flippedY = PANEL_HEIGHT - 1 - y;
    int srcRowOffset = flippedY * PANEL_WIDTH;
    
    for (int x = 0; x < PANEL_WIDTH; x++) {
      uint16_t pix = data[srcRowOffset + x];
      pix = ((pix & 0xFF) << 8) | ((pix & 0xFF00) >> 8);
      
      uint8_t r5 = (pix >> 11) & 0x1F;
      uint8_t g6 = (pix >> 5) & 0x3F;
      uint8_t b5 = pix & 0x1F;
      
      uint8_t r8 = (r5 << 3) | (r5 >> 2);
      uint8_t g8 = (g6 << 2) | (g6 >> 4);
      uint8_t b8 = (b5 << 3) | (b5 >> 2);
      
      ledArray[matrix->XY(x, y)] = CRGB(r8, g8, b8);
    }
  }
  FastLED.show();
}

void DrawTheFrame8FromBuffer(uint8_t* data){
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    int flippedY = PANEL_HEIGHT - 1 - y;
    int srcRowOffset = flippedY * PANEL_WIDTH;
    
    for (int x = 0; x < PANEL_WIDTH; x++) {
      uint8_t v = data[srcRowOffset + x];
      
      uint8_t r3 = (v & 0xE0) >> 5;
      uint8_t g3 = (v & 0x1C) >> 2;
      uint8_t b2 = (v & 0x03);
      
      uint8_t r8 = (r3 << 5) | (r3 << 2) | (r3 >> 1);
      uint8_t g8 = (g3 << 5) | (g3 << 2) | (g3 >> 1);
      uint8_t b8 = (b2 << 6) | (b2 << 4) | (b2 << 2) | b2;
      
      ledArray[matrix->XY(x, y)] = CRGB(r8, g8, b8);
    }
  }
  FastLED.show();
}

void DrawTheFrame24FromBuffer(uint8_t* data){
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    int flippedY = PANEL_HEIGHT - 1 - y;
    int srcRowOffset = (flippedY * PANEL_WIDTH) * 3;
    
    for (int x = 0; x < PANEL_WIDTH; x++) {
      int srcIdx = srcRowOffset + (x * 3);
      ledArray[matrix->XY(x, y)] = CRGB(data[srcIdx], data[srcIdx + 1], data[srcIdx + 2]);
    }
  }
  FastLED.show();
}