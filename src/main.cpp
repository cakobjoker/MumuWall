#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <FastLED_NeoMatrix.h>
#include <FastLED.h>
#include <HardwareSerial.h>
#include <driver/gpio.h>

// *** BOARD CONFIGURATION ***
// UART-only mode - all boards read from UART_IN jack

// Total display configuration (across all panels)
#define TOTAL_WIDTH 96    // Total width across all panels
#define TOTAL_HEIGHT 48   // Total height across all panels

// Hardware configuration
#define PIN 21 // GPIO21 for LED data 
#define BRIGHTNESS 24  // Very low for testing - reduces power draw
// #define DEBUG_LED 2

// Matrix configuration
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 16
#define MATRICES_WIDE 3
#define MATRICES_HIGH 3
#define PANEL_WIDTH (MATRIX_WIDTH * MATRICES_WIDE)    // 48
#define PANEL_HEIGHT (MATRIX_HEIGHT * MATRICES_HIGH)  // 48
#define NUM_LEDS (PANEL_WIDTH * PANEL_HEIGHT)

// Serial1 for forwarding to next panel group (UART_OUT)
// Using built-in Serial1 (no custom declaration needed)

static int width = PANEL_WIDTH;
static int height = PANEL_HEIGHT;
bool dataReceived = false;

// Multi-panel frame handling
size_t totalFrameSize = 0;
size_t myPanelOffset = 0;
bool is16BitFrame = false;
bool is24BitFrame = false;
uint8_t numPanelsInFrame = 0;  // Number of panels in current multi-panel command
uint8_t currentCommand = 0;     // Current command being processed

// Buffers
uint8_t* streamBuffer = nullptr;
size_t streamPos = 0;
size_t streamExpect = 0;
CRGB* ledArray = nullptr;

// Timeout detection
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;

// Matrix object
FastLED_NeoMatrix *matrix = nullptr;

// Forward declarations
void DrawTheFrame8FromBuffer(uint8_t* data, CRGB* target);
void DrawTheFrame16FromBuffer(uint16_t* data, CRGB* target);
void DrawTheFrame24FromBuffer(uint8_t* data, CRGB* target);
void handleCommand(uint8_t cmd);
void processStreamFrame();
void processByte(uint8_t byte);

void setup() {
  // // Setup debug LED
  // pinMode(DEBUG_LED, OUTPUT);
  // digitalWrite(DEBUG_LED, HIGH);
  
  // Initialize Serial0 (UART_IN) - default pins RXD0=GPIO44, TXD0=GPIO43
  Serial.begin(2000000);
  Serial.setTimeout(0);
  
  // Allocate LED buffer
  ledArray = (CRGB*)malloc(NUM_LEDS * sizeof(CRGB));
  if (ledArray == nullptr) {
    while(1) { delay(1000); }
  }
  memset(ledArray, 0, NUM_LEDS * sizeof(CRGB));
  
  // Allocate stream buffer for FULL frame (all panels)
  size_t fullFrameBufferSize = (size_t)(TOTAL_WIDTH * TOTAL_HEIGHT * 3);
  streamBuffer = (uint8_t*)malloc(fullFrameBufferSize);
  if (streamBuffer == nullptr) {
    free(ledArray);
    while(1) { delay(1000); }
  }
  
  // Initialize FastLED
  FastLED.addLeds<WS2812B, PIN, GRB>(ledArray, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();
  
  // Create matrix object
  matrix = new FastLED_NeoMatrix(ledArray, MATRIX_WIDTH, MATRIX_HEIGHT, MATRICES_WIDE, MATRICES_HIGH, 
    NEO_MATRIX_TOP + NEO_MATRIX_RIGHT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG + 
    NEO_TILE_TOP + NEO_TILE_LEFT + NEO_TILE_COLUMNS + NEO_TILE_ZIGZAG);
  
  if (matrix == nullptr) {
    while(1) { delay(1000); }
  }
  
  // Initialize Serial1 for UART communication
  // RX on GPIO44 (UART_IN tip = RXD0), TX on GPIO17 (UART_OUT tip)
  Serial1.begin(2000000, SERIAL_8N1, 44, 17);  // RX=44(RXD0), TX=17
  // Set GPIO17 (TX) to maximum drive strength for signal integrity
  gpio_set_drive_capability((gpio_num_t)17, GPIO_DRIVE_CAP_3);
  Serial1.setTimeout(0);
  
  // Final stabilization delay
  delay(500);
  
  // digitalWrite(DEBUG_LED, LOW);
}

void loop() {
  // Timeout detection - reset if stuck waiting for data
  if (streamExpect > 0 && (millis() - lastFrameTime > 5000)) {
    streamPos = 0;
    streamExpect = 0;
    lastFrameTime = millis();
  }
  
  // UART-only mode: Read from Serial1 (UART_IN jack from previous board)
  if (Serial1.available()) {
    // Process all available bytes
    while (Serial1.available()) {
      uint8_t byte = Serial1.read();
      processByte(byte);
    }
  }
}

void handleCommand(uint8_t cmd) {
  switch (cmd) {
  case 0x05:
    // Report total display size - ONLY Panel 0 responds
    // UART-only panels don't respond to dimension queries
    break;
  
  case 0x42:
    // Legacy 16-bit frame from LMCSHD - auto-calculate panel count
    numPanelsInFrame = (TOTAL_WIDTH / PANEL_WIDTH) * (TOTAL_HEIGHT / PANEL_HEIGHT);
    currentCommand = 0xC0 | numPanelsInFrame;
    totalFrameSize = (size_t)(TOTAL_WIDTH * TOTAL_HEIGHT);
    streamExpect = totalFrameSize * 2;
    streamPos = 0;
    is16BitFrame = true;
    lastFrameTime = millis();
    break;
  
  case 0x43:
    // Legacy 8-bit frame from LMCSHD - auto-calculate panel count
    numPanelsInFrame = (TOTAL_WIDTH / PANEL_WIDTH) * (TOTAL_HEIGHT / PANEL_HEIGHT);
    currentCommand = 0x80 | numPanelsInFrame;
    totalFrameSize = (size_t)(TOTAL_WIDTH * TOTAL_HEIGHT);
    streamExpect = totalFrameSize;
    streamPos = 0;
    is16BitFrame = false;
    lastFrameTime = millis();
    break;

  case 0x44:
    // Legacy 24-bit frame from LMCSHD - auto-calculate panel count
    numPanelsInFrame = (TOTAL_WIDTH / PANEL_WIDTH) * (TOTAL_HEIGHT / PANEL_HEIGHT);
    currentCommand = 0xE0 | numPanelsInFrame;
    totalFrameSize = (size_t)(TOTAL_WIDTH * TOTAL_HEIGHT);
    streamExpect = totalFrameSize * 3;
    streamPos = 0;
    is16BitFrame = false;
    is24BitFrame = true;
    lastFrameTime = millis();
    break;

  case 0xC1:
  case 0xC2:
  case 0xC3:
    // 16-bit multi-panel command - lower 4 bits = number of panels
    numPanelsInFrame = cmd & 0x0F;
    currentCommand = cmd;
    totalFrameSize = (size_t)(PANEL_WIDTH * PANEL_HEIGHT * numPanelsInFrame);
    streamExpect = totalFrameSize * 2;  // 16-bit = 2 bytes per pixel
    streamPos = 0;
    is16BitFrame = true;
    lastFrameTime = millis();
    break;

  case 0x81:
  case 0x82:
  case 0x83:
    // 8-bit multi-panel command - lower 4 bits = number of panels
    numPanelsInFrame = cmd & 0x0F;
    currentCommand = cmd;
    totalFrameSize = (size_t)(PANEL_WIDTH * PANEL_HEIGHT * numPanelsInFrame);
    streamExpect = totalFrameSize;  // 8-bit = 1 byte per pixel
    streamPos = 0;
    is16BitFrame = false;
    lastFrameTime = millis();
    break;

  case 0xC4:
    // 24-bit multi-panel command - lower 4 bits = number of panels
    numPanelsInFrame = cmd & 0x0F;
    currentCommand = cmd;
    totalFrameSize = (size_t)(PANEL_WIDTH * PANEL_HEIGHT * numPanelsInFrame);
    streamExpect = totalFrameSize * 3;  // 24-bit = 3 bytes per pixel
    streamPos = 0;
    is16BitFrame = false;
    is24BitFrame = true;
    lastFrameTime = millis();
    break;

  default:
    break;
  }
}

void processStreamFrame() {
  frameCount++;
  
  size_t bytesPerPanel = PANEL_WIDTH * PANEL_HEIGHT * (is16BitFrame ? 2 : 1);
  
  // Check if this is multi-panel data that needs extraction and forwarding
  bool isMultiPanelFrame = (numPanelsInFrame > 1);
  
  if (isMultiPanelFrame) {
    // Extract this panel's data (first panel in the stream)
    if (is16BitFrame) {
      for (int y = 0; y < PANEL_HEIGHT; y++) {
        int flippedY = PANEL_HEIGHT - 1 - y;
        for (int x = 0; x < PANEL_WIDTH; x++) {
          int srcIndex = flippedY * PANEL_WIDTH + x;
          uint16_t* data16 = (uint16_t*)streamBuffer;
          uint16_t pix = data16[srcIndex];
          pix = ((pix & 0xFF) << 8) | ((pix & 0xFF00) >> 8);
          
          uint8_t r5 = (pix >> 11) & 0x1F;
          uint8_t g6 = (pix >> 5) & 0x3F;
          uint8_t b5 = pix & 0x1F;
          
          int16_t ledIndex = matrix->XY(x, y);
          ledArray[ledIndex] = CRGB((r5 << 3) | (r5 >> 2), 
                                     (g6 << 2) | (g6 >> 4), 
                                     (b5 << 3) | (b5 >> 2));
        }
      }
    } else if (is24BitFrame) {
      for (int y = 0; y < PANEL_HEIGHT; y++) {
        int flippedY = PANEL_HEIGHT - 1 - y;
        for (int x = 0; x < PANEL_WIDTH; x++) {
          int srcIndex = (flippedY * PANEL_WIDTH + x) * 3;
          uint8_t r = streamBuffer[srcIndex];
          uint8_t g = streamBuffer[srcIndex + 1];
          uint8_t b = streamBuffer[srcIndex + 2];
          
          int16_t ledIndex = matrix->XY(x, y);
          ledArray[ledIndex] = CRGB(r, g, b);
        }
      }
    } else {
      for (int y = 0; y < PANEL_HEIGHT; y++) {
        int flippedY = PANEL_HEIGHT - 1 - y;
        for (int x = 0; x < PANEL_WIDTH; x++) {
          int srcIndex = flippedY * PANEL_WIDTH + x;
          uint8_t v = streamBuffer[srcIndex];
          
          int16_t ledIndex = matrix->XY(x, y);
          ledArray[ledIndex] = CRGB((v & 0xE0) | ((v & 0xE0) >> 3),
                                     ((v & 0x1C) << 3) | ((v & 0x1C)),
                                     ((v & 0x03) << 6) | ((v & 0x03) << 4) | ((v & 0x03) << 2) | (v & 0x03));
        }
      }
    }
    
    // Forward remaining panel data to next panel
    uint8_t nextHeader = (currentCommand & 0xF0) | (numPanelsInFrame - 1);
    size_t remainingPanelBytes = bytesPerPanel * (numPanelsInFrame - 1);
    
    if (remainingPanelBytes > 0) {
      uint8_t* forwardBuffer = (uint8_t*)malloc(remainingPanelBytes);
      if (forwardBuffer) {
        // Copy remaining panels' data
        memcpy(forwardBuffer, streamBuffer + bytesPerPanel, remainingPanelBytes);
        
        Serial1.write(nextHeader);
        Serial1.write(forwardBuffer, remainingPanelBytes);
        free(forwardBuffer);
      }
    }
  } else {
    // Single panel data - draw directly
    if (is16BitFrame) {
      DrawTheFrame16FromBuffer((uint16_t*)streamBuffer, ledArray);
    } else if (is24BitFrame) {
      DrawTheFrame24FromBuffer(streamBuffer, ledArray);
    } else {
      DrawTheFrame8FromBuffer(streamBuffer, ledArray);
    }
  }
  
  FastLED.show();
  
  dataReceived = true;
  lastFrameTime = millis();  // Update for next frame
}

void processByte(uint8_t byte) {
  if (streamExpect == 0) {
    // This is a command byte - validate before processing
    // Valid commands: 0x05, 0x42, 0x43, 0x81-0x83, 0xC1-0xC3
    if (byte == 0x05 || byte == 0x42 || byte == 0x43 || 
        byte == 0x81 || byte == 0x82 || byte == 0x83 ||
        byte == 0xC1 || byte == 0xC2 || byte == 0xC3) {
      handleCommand(byte);
    } else {
      // Invalid command byte - ignore to prevent desync
      // This helps when Panel 1 starts mid-frame
    }
  } else {
    // This is frame data
    if (streamPos < streamExpect) {
      streamBuffer[streamPos++] = byte;
      
      if (streamPos >= streamExpect) {
        // Frame complete
        processStreamFrame();
        streamPos = 0;
        streamExpect = 0;
      }
    } else {
      // Buffer overflow protection - reset state
      streamPos = 0;
      streamExpect = 0;
    }
  }
}

// Draw functions - render single panel data
void DrawTheFrame16FromBuffer(uint16_t* data, CRGB* target){
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    int flippedY = PANEL_HEIGHT - 1 - y;
    for (int x = 0; x < PANEL_WIDTH; x++) {
      // Data is for this single panel (48x48)
      int srcIndex = flippedY * PANEL_WIDTH + x;
      uint16_t pix = data[srcIndex];
      // Swap bytes
      pix = ((pix & 0xFF) << 8) | ((pix & 0xFF00) >> 8);
      
      uint8_t r5 = (pix >> 11) & 0x1F;
      uint8_t g6 = (pix >> 5) & 0x3F;
      uint8_t b5 = pix & 0x1F;
      
      int16_t ledIndex = matrix->XY(x, y);
      target[ledIndex] = CRGB((r5 << 3) | (r5 >> 2), 
                               (g6 << 2) | (g6 >> 4), 
                               (b5 << 3) | (b5 >> 2));
    }
  }
}

void DrawTheFrame8FromBuffer(uint8_t* data, CRGB* target){
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    int flippedY = PANEL_HEIGHT - 1 - y;
    for (int x = 0; x < PANEL_WIDTH; x++) {
      // Data is for this single panel (48x48)
      int srcIndex = flippedY * PANEL_WIDTH + x;
      uint8_t v = data[srcIndex];
      
      int16_t ledIndex = matrix->XY(x, y);
      target[ledIndex] = CRGB((v & 0xE0) | ((v & 0xE0) >> 3),
                               ((v & 0x1C) << 3) | ((v & 0x1C)),
                               ((v & 0x03) << 6) | ((v & 0x03) << 4) | ((v & 0x03) << 2) | (v & 0x03));
    }
  }
}

void DrawTheFrame24FromBuffer(uint8_t* data, CRGB* target){
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    int flippedY = PANEL_HEIGHT - 1 - y;
    for (int x = 0; x < PANEL_WIDTH; x++) {
      // Data is for this single panel (48x48)
      int srcIndex = (flippedY * PANEL_WIDTH + x) * 3;
      uint8_t r = data[srcIndex];
      uint8_t g = data[srcIndex + 1];
      uint8_t b = data[srcIndex + 2];
      
      int16_t ledIndex = matrix->XY(x, y);
      target[ledIndex] = CRGB(r, g, b);
    }
  }
}