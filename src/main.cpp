#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <FastLED_NeoMatrix.h>
#include <FastLED.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// *** BOARD CONFIGURATION ***
// Set to true for the first board (WiFi AP), false for UART-only boards
#define WIFI_AP_MODE true

// WiFi AP configuration
#define AP_SSID "MumuWall_AP"
#define AP_PASSWORD "mumuwall123"
#define UDP_PORT 7777

// Hardware configuration
#define PIN 21
#define BRIGHTNESS 24
#define DEBUG_LED 2

// Matrix configuration
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 16
#define MATRICES_WIDE 3
#define MATRICES_HIGH 3
#define PANEL_WIDTH (MATRIX_WIDTH * MATRICES_WIDE)    // 48
#define PANEL_HEIGHT (MATRIX_HEIGHT * MATRICES_HIGH)  // 48
#define NUM_LEDS (PANEL_WIDTH * PANEL_HEIGHT)

// Serial1 for forwarding to next panel group (UART_OUT)
HardwareSerial Serial1(1);

// WiFi UDP receiver (only used if WIFI_AP_MODE is true)
WiFiUDP udp;

static int width = PANEL_WIDTH;
static int height = PANEL_HEIGHT;
bool dataReceived = false;

// Buffers
uint8_t* streamBuffer = nullptr;
size_t streamPos = 0;
size_t streamExpect = 0;
CRGB* ledArray = nullptr;

// Matrix object
FastLED_NeoMatrix *matrix = nullptr;

// Forward declarations
void DrawTheFrame8FromBuffer(uint8_t* data, CRGB* target);
void DrawTheFrame16FromBuffer(uint16_t* data, CRGB* target);
void handleCommand(uint8_t cmd);
void processStreamFrame();

void setup() {
  // Setup debug LED
  pinMode(DEBUG_LED, OUTPUT);
  digitalWrite(DEBUG_LED, HIGH);
  
  // Initialize Serial0 (UART_IN) - default pins GPIO43=TX, GPIO44=RX
  Serial.begin(2000000);
  Serial.setTimeout(0);
  delay(500);
  
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
  
  // Allocate stream buffer
  Serial.println("Allocating stream buffer...");
  streamBuffer = (uint8_t*)malloc(NUM_LEDS * 3);
  if (streamBuffer == nullptr) {
    Serial.println("ERROR: Failed to allocate stream buffer!");
    free(ledArray);
    while(1) { delay(1000); }
  }
  Serial.printf("Stream buffer allocated. Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // Initialize FastLED
  Serial.println("Initializing FastLED...");
  FastLED.addLeds<WS2812B, PIN, GRB>(ledArray, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();
  Serial.println("FastLED initialized");
  
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
  
  // Initialize Serial1 for forwarding to next panel group
  // Using GPIO17=TX, GPIO18=RX (UART_OUT)
  Serial1.begin(2000000, SERIAL_8N1, 18, 17);
  Serial1.setTimeout(0);
  Serial.println("Serial1 initialized for panel forwarding");
  
#if WIFI_AP_MODE
  // Initialize WiFi Access Point
  Serial.println("\n=== Configuring WiFi Access Point ===");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  Serial.printf("SSID: %s\n", AP_SSID);
  Serial.printf("Password: %s\n", AP_PASSWORD);
  
  // Start UDP server
  udp.begin(UDP_PORT);
  Serial.printf("UDP server started on port %d\n", UDP_PORT);
  Serial.println("Ready to receive frames via WiFi");
#else
  Serial.println("\n*** UART-ONLY MODE - No WiFi AP ***");
#endif
  
  Serial.println("\n=== Setup complete! Ready for data ===");
  Serial.printf("Panel: %dx%d (%d LEDs)\n", PANEL_WIDTH, PANEL_HEIGHT, NUM_LEDS);
#if WIFI_AP_MODE
  Serial.printf("Mode: WiFi AP + UART Forwarding\n");
  Serial.printf("WiFi: UDP @ port %d\n", UDP_PORT);
#else
  Serial.printf("Mode: UART Input Only\n");
#endif
  Serial.printf("UART_IN: Serial0 @ 2000000 baud (GPIO43/44)\n");
  Serial.printf("UART_OUT: Serial1 @ 2000000 baud (GPIO17/18)\n");
  digitalWrite(DEBUG_LED, LOW);
}

void loop() {
#if WIFI_AP_MODE
  // Check for WiFi UDP packets
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    // Read UDP packet and forward to UART + process locally
    uint8_t buffer[2048];
    int len = udp.read(buffer, sizeof(buffer));
    
    // Forward all data to Serial1 (UART_OUT)
    Serial1.write(buffer, len);
    
    // Also process locally by writing to Serial (which the code below will read)
    Serial.write(buffer, len);
  }
#endif

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
      
      // Forward frame data byte to next panel group
      Serial1.write(byte);
      
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
  // Forward command to next panel group
  Serial1.write(cmd);
  
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
    // 16-bit multi-panel command
    if (Serial.available()) {
      Serial.read();
      streamExpect = (size_t)NUM_LEDS * 2;
      streamPos = 0;
    }
    break;

  case 0x81:
  case 0x82:
  case 0x83:
    // 8-bit multi-panel command
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
  // Write directly to LED array
  if (streamExpect == (size_t)NUM_LEDS * 2) {
    DrawTheFrame16FromBuffer((uint16_t*)streamBuffer, ledArray);
  } else if (streamExpect == (size_t)NUM_LEDS) {
    DrawTheFrame8FromBuffer(streamBuffer, ledArray);
  }
  
  // Show immediately
  FastLED.show();
  
  Serial.write(0x06);
  dataReceived = true;
}

// Draw functions using matrix->XY() directly
void DrawTheFrame16FromBuffer(uint16_t* data, CRGB* target){
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    int flippedY = PANEL_HEIGHT - 1 - y;
    int srcRowOffset = flippedY * PANEL_WIDTH;
    
    for (int x = 0; x < PANEL_WIDTH; x++) {
      uint16_t pix = data[srcRowOffset + x];
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
    int srcRowOffset = flippedY * PANEL_WIDTH;
    
    for (int x = 0; x < PANEL_WIDTH; x++) {
      uint8_t v = data[srcRowOffset + x];
      
      int16_t ledIndex = matrix->XY(x, y);
      target[ledIndex] = CRGB((v & 0xE0) | ((v & 0xE0) >> 3),
                               ((v & 0x1C) << 3) | ((v & 0x1C)),
                               ((v & 0x03) << 6) | ((v & 0x03) << 4) | ((v & 0x03) << 2) | (v & 0x03));
    }
  }
}