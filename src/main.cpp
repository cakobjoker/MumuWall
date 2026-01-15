#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <FastLED_NeoMatrix.h>
#include <FastLED.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <driver/gpio.h>

// *** BOARD CONFIGURATION ***
// Set to true for the first board (WiFi AP), false for UART-only boards
#define WIFI_AP_MODE false

// Total display configuration (across all panels)
// AP board uses this to calculate panel count
#define TOTAL_WIDTH 96    // Total width across all panels
#define TOTAL_HEIGHT 48   // Total height across all panels

// WiFi AP configuration
#define AP_SSID "MumuWall_AP"
#define AP_PASSWORD "mumuwall123"
#define UDP_PORT 7777

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

// WiFi UDP receiver (only used if WIFI_AP_MODE is true)
WiFiUDP udp;
IPAddress clientIP;  // Store client IP for responses
uint16_t clientPort; // Store client port for responses

static int width = PANEL_WIDTH;
static int height = PANEL_HEIGHT;
bool dataReceived = false;

// Multi-panel frame handling
size_t totalFrameSize = 0;
size_t myPanelOffset = 0;
bool is16BitFrame = false;
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
void handleCommand(uint8_t cmd);
void processStreamFrame();
void processByte(uint8_t byte);

void setup() {
  // // Setup debug LED
  // pinMode(DEBUG_LED, OUTPUT);
  // digitalWrite(DEBUG_LED, HIGH);
  
  // Initialize Serial0 (UART_IN) - default pins RXD0=GPIO44, TXD0=GPIO43
  Serial.begin(460800);
  Serial.setTimeout(0);
  delay(1000);  // Longer delay for stability
  
  Serial.println("\n=== ESP32-S3 LED Matrix Starting ===");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // Allocate LED buffer
  Serial.println("Allocating LED array...");
  ledArray = (CRGB*)malloc(NUM_LEDS * sizeof(CRGB));
  if (ledArray == nullptr) {
    Serial.println("ERROR: Failed to allocate LED array!");
    while(1) { delay(1000); }
  }
  memset(ledArray, 0, NUM_LEDS * sizeof(CRGB));
  Serial.printf("LED array allocated. Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // Allocate stream buffer for FULL frame (all panels)
  size_t fullFrameBufferSize = (size_t)(TOTAL_WIDTH * TOTAL_HEIGHT * 3);
  Serial.printf("Allocating stream buffer for full frame (%dx%d)...\n", TOTAL_WIDTH, TOTAL_HEIGHT);
  streamBuffer = (uint8_t*)malloc(fullFrameBufferSize);
  if (streamBuffer == nullptr) {
    Serial.println("ERROR: Failed to allocate stream buffer!");
    free(ledArray);
    while(1) { delay(1000); }
  }
  Serial.printf("Stream buffer allocated (%d bytes). Free heap: %d bytes\n", fullFrameBufferSize, ESP.getFreeHeap());
  
  // Initialize FastLED
  Serial.println("Initializing FastLED...");
  // IMPORTANT: Delay to let power stabilize before initializing LEDs
  delay(1000);
  
  FastLED.addLeds<WS2812B, PIN, GRB>(ledArray, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();
  Serial.println("FastLED initialized");
  
  // Additional delay after LED initialization
  delay(500);
  
  // Create matrix object
  Serial.println("Creating matrix...");
  matrix = new FastLED_NeoMatrix(ledArray, MATRIX_WIDTH, MATRIX_HEIGHT, MATRICES_WIDE, MATRICES_HIGH, 
    NEO_MATRIX_TOP + NEO_MATRIX_RIGHT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG + 
    NEO_TILE_TOP + NEO_TILE_LEFT + NEO_TILE_COLUMNS + NEO_TILE_ZIGZAG);
  
  if (matrix == nullptr) {
    Serial.println("ERROR: Failed to create matrix!");
    while(1) { delay(1000); }
  }
  Serial.printf("Matrix created. Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // Initialize Serial1 for UART communication
#if WIFI_AP_MODE
  // AP mode: TX on GPIO17 (UART_OUT tip), RX on GPIO18 (not used for receiving)
  Serial1.begin(460800, SERIAL_8N1, 18, 17);  // RX=18, TX=17
  // Set GPIO17 (TX) to maximum drive strength for signal integrity
  gpio_set_drive_capability((gpio_num_t)17, GPIO_DRIVE_CAP_3);
  Serial.println("Serial1 TX initialized on GPIO17 (UART_OUT tip) @ 2Mbps");
#else
  // Non-AP mode: RX on GPIO44 (UART_IN tip = RXD0), TX on GPIO17 (UART_OUT tip)
  Serial1.begin(460800, SERIAL_8N1, 44, 17);  // RX=44(RXD0), TX=17
  // Set GPIO17 (TX) to maximum drive strength for signal integrity
  gpio_set_drive_capability((gpio_num_t)17, GPIO_DRIVE_CAP_3);
  Serial.println("Serial1 initialized: RX=GPIO44 (UART_IN), TX=GPIO17 (UART_OUT) @ 2Mbps");
#endif
  Serial1.setTimeout(0);
  
  // CRITICAL: Add delay before WiFi init for stability
  delay(500);
  
#if WIFI_AP_MODE
  // Initialize WiFi Access Point
  Serial.println("\n=== Configuring WiFi Access Point ===");
  Serial.println("Setting WiFi mode to AP...");
  WiFi.mode(WIFI_AP);
  
  Serial.printf("Starting AP with SSID: %s\n", AP_SSID);
  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  if (apStarted) {
    Serial.println("✓ WiFi AP started successfully");
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    Serial.printf("SSID: %s\n", AP_SSID);
    Serial.printf("Password: %s\n", AP_PASSWORD);
    
    // Start UDP server
    udp.begin(UDP_PORT);
    Serial.printf("UDP server started on port %d\n", UDP_PORT);
    Serial.println("Ready to receive frames via WiFi");
  } else {
    Serial.println("✗ FAILED to start WiFi AP!");
    Serial.println("Check if another AP is running or ESP32 WiFi is working");
  }
#else
  Serial.println("\n*** UART-ONLY MODE - No WiFi AP ***");
#endif
  
  Serial.println("\n=== Setup complete! Ready for data ===");
  Serial.printf("Panel: %dx%d (%d LEDs)\n", PANEL_WIDTH, PANEL_HEIGHT, NUM_LEDS);
  Serial.printf("Total Display: %dx%d\n", TOTAL_WIDTH, TOTAL_HEIGHT);
#if WIFI_AP_MODE
  Serial.printf("Mode: WiFi AP + UART Forwarding\n");
  Serial.printf("WiFi: UDP @ port %d\n", UDP_PORT);
#else
  Serial.printf("Mode: UART Input Only\n");
#endif
  Serial.printf("UART_IN: Serial0 @ 460800 baud (GPIO43/44)\n");
  Serial.printf("UART_OUT: Serial1 @ 460800 baud (GPIO17/18)\n");
  
  // digitalWrite(DEBUG_LED, LOW);
}

void loop() {
  static unsigned long lastDebugTime = 0;
  static unsigned long loopCount = 0;
  loopCount++;
  
#if WIFI_AP_MODE
  // Test transmission every 5 seconds for debugging
  static unsigned long lastTestTx = 0;
  if (millis() - lastTestTx > 5000) {
    Serial1.println("TEST_FROM_PANEL_0");
    lastTestTx = millis();
  }
#endif
  
  // Print loop activity every 5 seconds when waiting
  if (streamExpect == 0 && millis() - lastDebugTime > 5000) {
    Serial.printf("Loop running (count: %lu), waiting for command. Client: %s:%d\n", 
                  loopCount, clientIP.toString().c_str(), clientPort);
    lastDebugTime = millis();
  }
  
  // Timeout detection - reset if stuck waiting for data
  if (streamExpect > 0 && (millis() - lastFrameTime > 5000)) {
    Serial.printf("TIMEOUT: Stuck waiting for frame data. Received %d of %d bytes. Resetting...\n", streamPos, streamExpect);
    streamPos = 0;
    streamExpect = 0;
    lastFrameTime = millis();
  }
  
#if WIFI_AP_MODE
  // Check for WiFi UDP packets
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    // Store client address for responses
    clientIP = udp.remoteIP();
    clientPort = udp.remotePort();
    
    // Read and process UDP packet byte by byte
    uint8_t buffer[4096];  // Larger buffer for full frames
    int len = udp.read(buffer, sizeof(buffer));
    
    // Show all incoming data for debugging
    if (len > 0) {
      Serial.printf("UDP: %d bytes, first byte: 0x%02X, expecting: %d\n", len, buffer[0], streamExpect);
    }
    
    // DON'T forward raw - let processStreamFrame handle forwarding with modified headers
    // Process each byte locally
    for (int i = 0; i < len; i++) {
      processByte(buffer[i]);
    }
  }
#endif

#if !WIFI_AP_MODE
  // UART-only mode: Read from Serial1 (UART_IN jack from previous board)
  static unsigned long lastUartCheck = 0;
  static unsigned long bytesReceived = 0;
  
  if (Serial1.available()) {
    // Process all available bytes
    while (Serial1.available()) {
      uint8_t byte = Serial1.read();
      bytesReceived++;
      processByte(byte);
    }
  }
  
  // Debug: Show UART activity every 2 seconds
  if (millis() - lastUartCheck > 2000) {
    Serial.printf("UART check: %lu bytes received total\n", bytesReceived);
    lastUartCheck = millis();
  }
#endif
}

void handleCommand(uint8_t cmd) {
  switch (cmd) {
  case 0x05:
    // Report total display size - ONLY Panel 0 responds
    Serial.printf("Dimension query (0x05) received\n");
    
#if WIFI_AP_MODE
    // Send response via UDP back to client
    if (clientPort > 0) {
      char response[32];
      // Protocol requires: "width\nheight\n" (each on separate line)
      int len = snprintf(response, sizeof(response), "%d\n%d\n", TOTAL_WIDTH, TOTAL_HEIGHT);
      udp.beginPacket(clientIP, clientPort);
      udp.write((uint8_t*)response, len);
      udp.endPacket();
      Serial.printf("→ Sent dimension response: %d\\n%d\\n (%d bytes)\n", TOTAL_WIDTH, TOTAL_HEIGHT, len);
    } else {
      Serial.println("WARNING: No client port, cannot send response");
    }
#else
    // UART-only panels don't respond to dimension queries
    Serial.println("(Panel ignoring dimension query - only AP panel responds)");
#endif
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
    Serial.printf("Command 0x42: %d panels, expecting %d bytes\n", numPanelsInFrame, streamExpect);
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
    Serial.printf("Command 0x43: %d panels, expecting %d bytes\n", numPanelsInFrame, streamExpect);
    break;

  case 0x44:
    // 24-bit not supported
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
    Serial.printf("Command 0x%02X: %d panels, expecting %d bytes (16-bit)\n", cmd, numPanelsInFrame, streamExpect);
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
    Serial.printf("Command 0x%02X: %d panels, expecting %d bytes (8-bit)\n", cmd, numPanelsInFrame, streamExpect);
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
  frameCount++;
  Serial.printf("Frame #%lu: %d bytes, panels=%d, cmd=0x%02X\n", 
                frameCount, streamPos, numPanelsInFrame, currentCommand);
  
  size_t bytesPerPanel = PANEL_WIDTH * PANEL_HEIGHT * (is16BitFrame ? 2 : 1);
  
  // Check if this is full-width data (from LMCSHD) or already-extracted panel data (forwarded)
  bool isFullWidthFrame = (streamExpect == TOTAL_WIDTH * TOTAL_HEIGHT * (is16BitFrame ? 2 : 1));
  
  if (isFullWidthFrame && numPanelsInFrame > 1) {
    // This is Panel 0 receiving full 96x48 frame - extract left portion
    int panelXOffset = 0;  // This is the leftmost panel
    Serial.printf("Full frame: drawing panel at X offset %d\n", panelXOffset);
    
    if (is16BitFrame) {
      for (int y = 0; y < PANEL_HEIGHT; y++) {
        int flippedY = PANEL_HEIGHT - 1 - y;
        for (int x = 0; x < PANEL_WIDTH; x++) {
          int srcIndex = flippedY * TOTAL_WIDTH + panelXOffset + x;
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
    } else {
      for (int y = 0; y < PANEL_HEIGHT; y++) {
        int flippedY = PANEL_HEIGHT - 1 - y;
        for (int x = 0; x < PANEL_WIDTH; x++) {
          int srcIndex = flippedY * TOTAL_WIDTH + panelXOffset + x;
          uint8_t v = streamBuffer[srcIndex];
          
          int16_t ledIndex = matrix->XY(x, y);
          ledArray[ledIndex] = CRGB((v & 0xE0) | ((v & 0xE0) >> 3),
                                     ((v & 0x1C) << 3) | ((v & 0x1C)),
                                     ((v & 0x03) << 6) | ((v & 0x03) << 4) | ((v & 0x03) << 2) | (v & 0x03));
        }
      }
    }
    
    // Forward extracted right panel data
    uint8_t nextHeader = (currentCommand & 0xF0) | (numPanelsInFrame - 1);
    size_t nextPanelBytes = bytesPerPanel * (numPanelsInFrame - 1);
    
    uint8_t* forwardBuffer = (uint8_t*)malloc(nextPanelBytes);
    if (forwardBuffer) {
      if (is16BitFrame) {
        uint16_t* srcData = (uint16_t*)streamBuffer;
        uint16_t* dstData = (uint16_t*)forwardBuffer;
        for (int y = 0; y < PANEL_HEIGHT; y++) {
          for (int x = 0; x < PANEL_WIDTH; x++) {
            int srcIdx = y * TOTAL_WIDTH + PANEL_WIDTH + x;
            int dstIdx = y * PANEL_WIDTH + x;
            dstData[dstIdx] = srcData[srcIdx];
          }
        }
      } else {
        for (int y = 0; y < PANEL_HEIGHT; y++) {
          for (int x = 0; x < PANEL_WIDTH; x++) {
            int srcIdx = y * TOTAL_WIDTH + PANEL_WIDTH + x;
            int dstIdx = y * PANEL_WIDTH + x;
            forwardBuffer[dstIdx] = streamBuffer[srcIdx];
          }
        }
      }
      
      Serial.printf("Forwarding: 0x%02X + %d bytes\n", nextHeader, nextPanelBytes);
      Serial1.write(nextHeader);
      Serial1.write(forwardBuffer, nextPanelBytes);
      free(forwardBuffer);
    }
  } else {
    // This is already-extracted panel data (48x48) - use simple draw
    Serial.printf("Panel data: drawing from offset 0\n");
    if (is16BitFrame) {
      DrawTheFrame16FromBuffer((uint16_t*)streamBuffer, ledArray);
    } else {
      DrawTheFrame8FromBuffer(streamBuffer, ledArray);
    }
  }
  
  FastLED.show();
  Serial.printf("Frame #%lu displayed!\n", frameCount);
  
  // Send ACK via UDP back to client
#if WIFI_AP_MODE
  if (clientPort > 0) {
    uint8_t ack = 0x06;
    udp.beginPacket(clientIP, clientPort);
    udp.write(&ack, 1);
    udp.endPacket();
    Serial.println("ACK sent via UDP");
  }
#endif
  
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
      static unsigned long lastWarning = 0;
      if (millis() - lastWarning > 1000) {
        Serial.printf("Ignoring invalid command byte: 0x%02X\n", byte);
        lastWarning = millis();
      }
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
      Serial.printf("ERROR: Buffer overflow! streamPos=%d, streamExpect=%d\n", streamPos, streamExpect);
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