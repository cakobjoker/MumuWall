# MumuWall Diagnostics - What to Look For

## Setup Phase (Should appear first after upload)
```
=== MumuWall ESP32-S3 Starting ===
Heap: XXXXX bytes
Panel size: 48x48 (2304 LEDs)
Total display: 48x48
LED Pin: GPIO5
Allocating LED array...
✓ LED array allocated: 2304 LEDs
Allocating stream buffer...
✓ Stream buffer allocated: 6912 bytes
FastLED initialized on GPIO5, brightness=255
Matrix created successfully
Serial1 initialized: RX=GPIO44, TX=GPIO17 @ 2Mbps
Waiting for UART data...
Setup complete! Waiting for UART data...
```

If you don't see these messages, there's an early crash or Serial initialization failure.

## Command Reception
When UART data arrives, expect to see:
```
CMD: 0x43 (legacy 8-bit), panels=1, expect 2304 bytes
```

OR for 16-bit:
```
CMD: 0x42 (legacy 16-bit), panels=1, expect 4608 bytes
```

OR for 24-bit:
```
CMD: 0x44 (legacy 24-bit), panels=1, expect 6912 bytes
```

## Frame Data Reception
As data arrives, you should see progress updates:
```
  [Frame RX: 1024/2304 bytes]
  [Frame RX: 2048/2304 bytes]
```

(Only prints every 1024 bytes received)

## Frame Completion
When a complete frame has been received:
```
✓ FRAME COMPLETE: received 2304 bytes, processing...
FRAME #1: 2304 bytes received, format=8-bit, panels=1
  → Drawing single panel frame...
  → Calling FastLED.show()...
✓ Frame displayed!
```

## Troubleshooting Guide

### Issue: No setup messages appear
- **Cause**: Serial initialization failure or crash before setup
- **Fix**: Check USB cable, try different port, verify ESP32 is entering bootloader

### Issue: Setup messages appear but no "Setup complete!" 
- **Cause**: Crash after Serial initialization (likely in FastLED or matrix setup)
- **Fix**: Check if heap is sufficient (should be >50KB), verify FastLED library is correct version

### Issue: Commands appear but no "FRAME COMPLETE" messages
- **Cause**: Frame data not arriving completely or wrong baud rate
- **Fix**: 
  - Verify sender is set to 2000000 baud
  - Check UART_IN jack connection
  - Try sending data multiple times

### Issue: "FRAME COMPLETE" appears but "FRAME #X:" doesn't
- **Cause**: Bug in processStreamFrame() - check if frame processing is executing
- **Fix**: Look for "Drawing single panel frame..." message; if not there, there's a processing logic bug

### Issue: "FRAME #X:" and drawing messages appear but no "Frame displayed!"
- **Cause**: FastLED.show() is hanging or failing
- **Fix**: 
  - Check GPIO5 connection to LED data line
  - Verify LED power supply (should be 5V @ significant current)
  - Try shorter LED chain first for testing

### Issue: Everything displays but LEDs don't light up
- **Cause**: LED initialization, pixel format, or power issues
- **Fix**:
  - Verify WS2812B/NeoPixel LEDs have power and ground connected
  - Check if first LED works with simple test pattern
  - Verify GRB color order is correct (not RGB)
