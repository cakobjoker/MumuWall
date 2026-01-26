// Include necessary libraries
#include <Adafruit_GFX.h>
#include <FastLED_NeoMatrix.h>
#include <FastLED.h>
#include <HardwareSerial.h>

// Create HardwareSerial for UART0 (UART_IN jack on custom board)
HardwareSerial uart_in(0);

//--------------- Change as Needed ---------------------------------------
// Define constants for the LED matrix
#define PIN 21              // GPIO pin for WS2812B data line (ESP32-S3-mini-1)
#define BRIGHTNESS 32      // Brightness of the LED matrix (out of 255)


// Number of panels in the setup
uint8_t NUM_PANELS = 4;

// Total display dimensions for calculating panel layout
#define TOTAL_WIDTH 96       // Total width in pixels (48 per panel)
#define TOTAL_HEIGHT 96      // Total height in pixels
#define PANEL_WIDTH 48       // Width of one panel
#define PANEL_HEIGHT 48      // Height of one panel
#define NUM_MATRIX (PANEL_WIDTH*PANEL_HEIGHT)  // Total number of LEDs in one panel = 2304
// Auto-calculated from dimensions
#define NUM_PANELS_WIDE (TOTAL_WIDTH / PANEL_WIDTH)  // Panels horizontally
#define NUM_PANELS_HIGH (TOTAL_HEIGHT / PANEL_HEIGHT) // Panels vertically

//--------------- Change as Needed ---------------------------------------

// Array to hold the LED color data
CRGB leds[NUM_MATRIX];

// Global tracking for panel positioning
uint16_t PanelDrawX = 0;  // X position where this panel draws
uint16_t PanelDrawY = 0;  // Y position where this panel draws

// Create a new NeoMatrix object
// 3x3 grid of 16x16 matrices = 48x48 pixels per panel
FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(leds, 16, 16, 3, 3, 
  NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG + 
  NEO_TILE_TOP + NEO_TILE_RIGHT +  NEO_TILE_COLUMNS + NEO_TILE_ZIGZAG);

// Arrays to hold the draw data and pass data
uint16_t DrawData[NUM_MATRIX]; 
uint8_t PassData[NUM_MATRIX * (2 * 2)]; // NUM_PANELS * 2
uint8_t * RawData8 = NULL;
uint16_t * RawData16 = NULL;
uint16_t PanelIndex = 0;  // Track which panel this board should draw

// Forward declarations of functions
void GetTheData8();
void GetTheData16();
void DrawTheFrame8();
void DrawTheFrame16();

void setup() {
  // Initialize serial communication first for debugging
  Serial.begin(2000000);
  delay(500);
  Serial.println("\n=== MumuWall ESP32-S3 Starting ===");
  Serial.print("Panel size: ");
  Serial.print(PANEL_WIDTH);
  Serial.print("x");
  Serial.println(PANEL_HEIGHT);
  Serial.print("Total panels: ");
  Serial.println(NUM_PANELS);
  Serial.print("LED Pin: GPIO");
  Serial.println(PIN);
  
  // Add LEDs to the FastLED library
  FastLED.addLeds<WS2812B, PIN, GRB>(leds, NUM_MATRIX);
  FastLED.setBrightness(BRIGHTNESS);
  
  // Initialize the matrix
  matrix->begin();
  matrix->setBrightness(BRIGHTNESS);
  
  // Test pattern - fill with red to verify hardware
  Serial.println("Running LED test pattern...");
  for (int i = 0; i < NUM_MATRIX; i++) {
    leds[i] = CRGB::Red;
  }
  FastLED.show();
  Serial.println("LEDs should be RED now");
  delay(2000);
  
  // Clear the matrix
  for (int i = 0; i < NUM_MATRIX; i++) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();
  Serial.println("LEDs should be OFF now");
  
  // Initialize Serial1 for UART_OUT (panel chaining - GPIO17/18)
  Serial1.setPins(18, 17);  
  Serial1.begin(2000000);
  Serial.println("Serial1 initialized: RX=GPIO18, TX=GPIO17 @ 2Mbps (UART_OUT)");
  
  // Initialize uart_in for UART_IN (GPIO43/44 - hardware UART0)
  // RX=GPIO43 (RXD0/Ring), TX=GPIO44 (TXD0/Tip)
  // USB adapter: Tip=TX → Board RX, Ring=RX ← Board TX
  // So swap: RX should be GPIO44, TX should be GPIO43
  uart_in.begin(2000000, SERIAL_8N1, 44, 43);
  Serial.println("UART_IN initialized: RX=GPIO44(Tip), TX=GPIO43(Ring) @ 2Mbps (UART_IN jack)");
  
  delay(500);
  Serial.println("Setup complete! Waiting for UART data...");
}

void GetTheData8(){
  // Always read the full frame
  uint16_t bytesToRead = TOTAL_WIDTH * TOTAL_HEIGHT;
  uart_in.readBytes(PassData, bytesToRead);  
  
  // Set up for drawing
  RawData8 = PassData;
  PanelIndex = 0;
  
  // Calculate board position in generic snake pattern for any grid size
  uint16_t position = (NUM_PANELS_WIDE * NUM_PANELS_HIGH) - NUM_PANELS;
  uint8_t snake_column = position / NUM_PANELS_HIGH;
  uint8_t position_in_column = position % NUM_PANELS_HIGH;
  
  // Determine row: even columns go up, odd columns go down
  uint8_t snake_row = (snake_column % 2 == 0) ? 
    (NUM_PANELS_HIGH - 1 - position_in_column) : position_in_column;
  
  // Convert grid position to pixel coordinates
  PanelDrawX = snake_column * PANEL_WIDTH;
  PanelDrawY = snake_row * PANEL_HEIGHT;
  
  // FORWARD FIRST - let next board start processing while we draw
  if (NUM_PANELS > 1) {
    Serial1.write(0x80 | (NUM_PANELS - 1));  // Header with decremented panel count
    Serial1.write(PassData, bytesToRead); 
  }
  
  // Now draw our frame
  DrawTheFrame8(); 
  
  // Send acknowledgement
  uart_in.write(0x06); 
}

void GetTheData16(){
  // Always read the full frame
  uint16_t bytesToRead = TOTAL_WIDTH * TOTAL_HEIGHT * 2;
  uart_in.readBytes(PassData, bytesToRead);  
  
  // Set up for drawing
  RawData16 = (uint16_t *) PassData;
  PanelIndex = 0;
  
  // Calculate board position in generic snake pattern for any grid size
  uint16_t position = (NUM_PANELS_WIDE * NUM_PANELS_HIGH) - NUM_PANELS;
  uint8_t snake_column = position / NUM_PANELS_HIGH;
  uint8_t position_in_column = position % NUM_PANELS_HIGH;
  
  // Determine row: even columns go up, odd columns go down
  uint8_t snake_row = (snake_column % 2 == 0) ? 
    (NUM_PANELS_HIGH - 1 - position_in_column) : position_in_column;
  
  // Convert grid position to pixel coordinates
  PanelDrawX = snake_column * PANEL_WIDTH;
  PanelDrawY = snake_row * PANEL_HEIGHT;
  
  // FORWARD FIRST - let next board start processing while we draw
  if (NUM_PANELS > 1) {
    Serial1.write(0xC0 | (NUM_PANELS - 1));  // Header with decremented panel count
    Serial1.write(PassData, bytesToRead); 
  }
  
  // Now draw our frame
  DrawTheFrame16(); 
  
  // Send acknowledgement
  uart_in.write(0x06); 
}

void DrawTheFrame8(){
  // Data arrives in row-major order for full frame
  // Extract this board's 48x48 section based on PanelDrawX and PanelDrawY
  
  uint16_t xOffset = PanelDrawX;
  uint16_t yOffset = PanelDrawY;
  
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    for (int x = 0; x < PANEL_WIDTH; x++) {
      int bufferIndex = (yOffset + y) * TOTAL_WIDTH + (xOffset + x);
      int drawX = PANEL_WIDTH - 1 - x;
      int drawY = PANEL_HEIGHT - 1 - y;
      
      uint8_t pixelData = RawData8[bufferIndex];
      uint16_t color = ((pixelData & 0xE0) << 8) | ((pixelData & 0x1C) << 6) | ((pixelData & 0x03) << 3);
      color |= (pixelData & 0x03) < 2 ? 0 : 4;
      
      matrix->drawPixel(drawX, drawY, color);
    }
  }
  
  FastLED.show();
}

void DrawTheFrame16(){
  // Data arrives in row-major order for full frame
  // Extract this board's 48x48 section based on PanelDrawX and PanelDrawY
  
  uint16_t xOffset = PanelDrawX;
  uint16_t yOffset = PanelDrawY;

  for (int y = 0; y < PANEL_HEIGHT; y++) {
    for (int x = 0; x < PANEL_WIDTH; x++) {
      int bufferIndex = (yOffset + y) * TOTAL_WIDTH + (xOffset + x);
      int drawX = PANEL_WIDTH - 1 - x;
      int drawY = PANEL_HEIGHT - 1 - y;
      
      uint16_t color = ((RawData16[bufferIndex] & 0xFF) << 8) | ((RawData16[bufferIndex] & 0xFF00) >> 8);
      
      matrix->drawPixel(drawX, drawY, color);
    }
  }
  
  FastLED.show();
}

void loop() {
  // Check if data is available on uart_in (UART_IN jack)
  if (uart_in.available()) {
    // Read header from the UART_IN
    uint8_t header = uart_in.read();
    Serial.print("Received header: 0x");
    Serial.println(header, HEX);
    
    if (header == 0x05){
      // If the header is 0x05, print the width and height of the LED matrix
      Serial.print("INFO REQUEST: Display size = ");
      Serial.print(TOTAL_WIDTH);
      Serial.print("x");
      Serial.println(TOTAL_HEIGHT);
      uart_in.write(TOTAL_WIDTH);
      uart_in.write(TOTAL_HEIGHT);
    }
    else if (header == 0x42){
      // Read the data from the UART_IN (16-bit single panel)
      GetTheData16();
    }
    else if (header == 0x43){
      // Read the data from the UART_IN (8-bit single panel)
      GetTheData8();
    }
    else if (header == 0xC3 || header == 0xC2 || header == 0xC1){
      // Set the number of panels and read the data from the UART_IN (16-bit multi-panel)
      NUM_PANELS = header & 0x0F;
      Serial.print("Multi-panel 16-bit mode, NUM_PANELS=");
      Serial.println(NUM_PANELS);
      GetTheData16();
    }
    else if (header == 0x83 || header == 0x82 || header == 0x81){
      // Set the number of panels and read the data from the UART_IN (8-bit multi-panel)
      NUM_PANELS = header & 0x0F;
      Serial.print("Multi-panel 8-bit mode, NUM_PANELS=");
      Serial.println(NUM_PANELS);
      GetTheData8();
    }
  }
}