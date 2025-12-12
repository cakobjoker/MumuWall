#include <Arduino.h>

// Include necessary libraries
#include <Adafruit_GFX.h>
#include <FastLED_NeoMatrix.h>
#include <FastLED.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>


// Define constants for the LED matrix
#define PIN 21
#define BRIGHTNESS 32      // Brightness of the LED matrix (out of 255)

// Individual matrix size (each 16×16 matrix module)
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 16

// Configuration: number of matrices in each direction
#define MATRICES_WIDE 3     // 3 matrices wide
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

static int width = PANEL_WIDTH;
static int height = PANEL_HEIGHT;
bool dataReceived = false;

// Streaming buffer for non-blocking reads
uint8_t streamBuffer[NUM_LEDS * 3];
size_t streamPos = 0;
size_t streamExpect = 0;

// Pre-calculated XY to physical LED index mapping
int16_t xyMap[PANEL_HEIGHT][PANEL_WIDTH];

// Double buffer for zero-copy frame swaps
CRGB ledArray[NUM_LEDS];
CRGB swapArray[NUM_LEDS];
CRGB* activeArray = ledArray;
CRGB* inactiveArray = swapArray;
portMUX_TYPE swapMutex = portMUX_INITIALIZER_UNLOCKED;
volatile bool frameReady = false;  // Signal that frame is ready to display

// Create a new NeoMatrix object (moved before initXYMap declaration)
FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(ledArray, MATRIX_WIDTH, MATRIX_HEIGHT, MATRICES_WIDE, MATRICES_HIGH, 
  NEO_MATRIX_TOP     + NEO_MATRIX_RIGHT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG + 
  NEO_TILE_TOP + NEO_TILE_LEFT +  NEO_TILE_COLUMNS + NEO_TILE_ZIGZAG);

// Forward declarations
void DrawTheFrame8FromBuffer(uint8_t* data, CRGB* target);
void DrawTheFrame16FromBuffer(uint16_t* data, CRGB* target);
void handleCommand(uint8_t cmd);
void processStreamFrame();
void initXYMap();
void refreshTask(void* pvParameters);

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
  
  // Pre-calculate XY mapping table
  initXYMap();
  
  // Initialize serial communication at standard baud rate
  // Official ESP32 UART speeds: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600
  // However, 460800 and 921600 can be unreliable. Use 230400 as safe maximum.
  Serial.begin(230400);
  Serial.setTimeout(0); // Non-blocking mode
  
  // Create refresh task on core 0 (leaves core 1 for serial/main)
  xTaskCreatePinnedToCore(
    refreshTask,
    "RefreshLEDs",
    2048,
    NULL,
    1,
    NULL,
    0
  );
}

// Background task that continuously refreshes LEDs
void refreshTask(void* pvParameters) {
  while (1) {
    if (frameReady) {
      // Only show when a new frame is ready
      FastLED.show();
      frameReady = false;
    }
    // Yield to prevent watchdog timeout
    vTaskDelay(1);
  }
}

void initXYMap() {
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    for (int x = 0; x < PANEL_WIDTH; x++) {
      xyMap[y][x] = matrix->XY(x, y);
    }
  }
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
    // 24-bit not supported
    break;

  case 0xC1:
  case 0xC2:
  case 0xC3:
    // 16-bit multi-panel command (for future support)
    if (Serial.available()) {
      Serial.read();
      streamExpect = (size_t)NUM_LEDS * 2;
      streamPos = 0;
    }
    break;

  case 0x81:
  case 0x82:
  case 0x83:
    // 8-bit multi-panel command (for future support)
    if (Serial.available()) {
      Serial.read();
      streamExpect = (size_t)NUM_LEDS;
      streamPos = 0;
    }
    break;

  case 0xC4:
    // 24-bit not supported
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
  // Write to inactive buffer
  if (streamExpect == (size_t)NUM_LEDS * 2) {
    DrawTheFrame16FromBuffer((uint16_t*)streamBuffer, inactiveArray);
  } else if (streamExpect == (size_t)NUM_LEDS) {
    DrawTheFrame8FromBuffer(streamBuffer, inactiveArray);
  }
  
  // Swap buffers atomically and signal refresh task
  portENTER_CRITICAL(&swapMutex);
  CRGB* temp = activeArray;
  activeArray = inactiveArray;
  inactiveArray = temp;
  frameReady = true;
  portEXIT_CRITICAL(&swapMutex);
  
  Serial.write(0x06);
  dataReceived = true;
}

// Updated draw functions - write to target buffer instead of ledArray
void DrawTheFrame16FromBuffer(uint16_t* data, CRGB* target){
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    int flippedY = PANEL_HEIGHT - 1 - y;
    int srcRowOffset = flippedY * PANEL_WIDTH;
    int16_t* rowMap = xyMap[y];
    
    for (int x = 0; x < PANEL_WIDTH; x++) {
      uint16_t pix = data[srcRowOffset + x];
      pix = ((pix & 0xFF) << 8) | ((pix & 0xFF00) >> 8);
      
      uint8_t r5 = (pix >> 11) & 0x1F;
      uint8_t g6 = (pix >> 5) & 0x3F;
      uint8_t b5 = pix & 0x1F;
      
      target[rowMap[x]] = CRGB((r5 << 3) | (r5 >> 2), 
                               (g6 << 2) | (g6 >> 4), 
                               (b5 << 3) | (b5 >> 2));
    }
  }
}

void DrawTheFrame8FromBuffer(uint8_t* data, CRGB* target){
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    int flippedY = PANEL_HEIGHT - 1 - y;
    int srcRowOffset = flippedY * PANEL_WIDTH;
    int16_t* rowMap = xyMap[y];
    
    for (int x = 0; x < PANEL_WIDTH; x++) {
      uint8_t v = data[srcRowOffset + x];
      
      target[rowMap[x]] = CRGB((v & 0xE0) | ((v & 0xE0) >> 3),
                               ((v & 0x1C) << 3) | ((v & 0x1C)),
                               ((v & 0x03) << 6) | ((v & 0x03) << 4) | ((v & 0x03) << 2) | (v & 0x03));
    }
  }
}