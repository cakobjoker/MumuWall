# MumuWall Multi-Panel Setup Guide

## Configuration for 2 ESP32s (Side-by-Side Layout)

### ESP32 #1 (Left Panel - WiFi AP Mode)
Edit `main.cpp` before uploading:
```cpp
#define WIFI_AP_MODE true      // Enable WiFi AP
#define PANEL_NUMBER 0         // First panel (left)
#define TOTAL_WIDTH 96         // Total: 2 panels × 48 pixels
#define TOTAL_HEIGHT 48        // Height: 48 pixels
#define LAYOUT_HORIZONTAL true // Side-by-side
```

### ESP32 #2 (Right Panel - UART Only)
Edit `main.cpp` before uploading:
```cpp
#define WIFI_AP_MODE false     // UART only, no WiFi
#define PANEL_NUMBER 1         // Second panel (right)
#define TOTAL_WIDTH 96         // Total: 2 panels × 48 pixels
#define TOTAL_HEIGHT 48        // Height: 48 pixels
#define LAYOUT_HORIZONTAL true // Side-by-side
```

## Hardware Connections

### ESP32 #1 → ESP32 #2
Connect UART_OUT to UART_IN:
- **ESP32 #1 GPIO17 (TX)** → **ESP32 #2 GPIO44 (RX)**
- **ESP32 #1 GND** → **ESP32 #2 GND**

Baud rate: **2,000,000** (2 Mbaud)

## Software Setup

### 1. Upload Code to Both ESP32s
- Configure each board as shown above
- Upload via PlatformIO

### 2. Connect to WiFi AP
- Network: **MumuWall_AP**
- Password: **mumuwall123**
- AP IP: **192.168.4.1** (default)

### 3. Start the Serial-to-UDP Bridge
```powershell
python serial_to_udp_bridge.py COM12 192.168.4.1
```

### 4. Configure LMCSHD
- Port: **COM11** (paired with COM12)
- Baud rate: **2000000**
- Resolution: **96 × 48** pixels

When LMCSHD asks for dimensions, enter:
- Width: **96**
- Height: **48**

## Data Flow

```
LMCSHD (96×48 frame)
    ↓
  COM11 ⟷ COM12
    ↓
Python Bridge
    ↓
UDP (192.168.4.1:7777)
    ↓
ESP32 #1 (displays left 48×48, forwards all via UART)
    ↓
ESP32 #2 (displays right 48×48)
```

## Performance

- Baud rate: **2 Mbaud**
- Max frame rate: ~**43 FPS** (theoretical)
- Actual FPS depends on:
  - WiFi latency (~5-20ms)
  - Cable quality (use short, good quality cables)
  - Frame size

## Troubleshooting

### No Display on ESP32 #1
- Check WiFi connection
- Verify bridge is running
- Check Serial Monitor for errors

### No Display on ESP32 #2
- Verify UART connection (GPIO17→GPIO44, GND→GND)
- Check cable quality (2 Mbaud requires good cables)
- Try shorter cables (<1-2 meters)

### Display Shows Wrong Portion
- Verify `PANEL_NUMBER` is correct (0 for left, 1 for right)
- Check `LAYOUT_HORIZONTAL` is `true`
- Verify `TOTAL_WIDTH` and `TOTAL_HEIGHT` match

### Low Frame Rate
- Use shorter UART cables
- Reduce LED count or brightness
- Check WiFi signal strength

## Alternative Layouts

### Vertical Stack (Top/Bottom)
```cpp
#define TOTAL_WIDTH 48          // Width: 48 pixels
#define TOTAL_HEIGHT 96         // Total: 2 panels × 48 pixels
#define LAYOUT_HORIZONTAL false // Stacked vertically
```

- Top panel: `PANEL_NUMBER 0`
- Bottom panel: `PANEL_NUMBER 1`
- LMCSHD resolution: **48 × 96**
