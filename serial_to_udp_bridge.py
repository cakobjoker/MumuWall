#!/usr/bin/env python3
"""
Bidirectional Serial to UDP Bridge for MumuWall
Forwards data from LMCSHD (serial) to ESP32 (UDP) AND responses back

Usage:
    python serial_to_udp_bridge.py COM3 192.168.4.1
    
Where COM3 is your virtual serial port and 192.168.4.1 is the ESP32 AP IP
"""

import socket
import sys
import serial
import time
import select

def main():
    if len(sys.argv) < 3:
        print("Usage: python serial_to_udp_bridge.py <SERIAL_PORT> <ESP32_IP>")
        print("Example: python serial_to_udp_bridge.py COM3 192.168.4.1")
        sys.exit(1)
    
    serial_port = sys.argv[1]
    esp32_ip = sys.argv[2]
    udp_port = 7777
    baud_rate = 460800  # Match ESP32 baud rate
    
    print(f"=== Bidirectional Serial to UDP Bridge ===")
    print(f"Serial Port: {serial_port} @ {baud_rate} baud")
    print(f"UDP Target: {esp32_ip}:{udp_port}")
    print(f"Connecting...\n")
    
    try:
        # Open serial port
        ser = serial.Serial(serial_port, baud_rate, timeout=0)
        print(f"✓ Serial port opened")
        
        # Create UDP socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('', 0))  # Bind to any available port for receiving
        sock.settimeout(0)  # Non-blocking
        print(f"✓ UDP socket created")
        print(f"\nBridge active! Bidirectional forwarding")
        print(f"Press Ctrl+C to stop\n")
        
        bytes_to_esp = 0
        bytes_from_esp = 0
        packets_to_esp = 0
        packets_from_esp = 0
        
        while True:
            # Read from serial (LMCSHD → ESP32)
            if ser.in_waiting > 0:
                # Read available data (up to larger buffer for frames)
                data = ser.read(min(ser.in_waiting, 8192))
                
                # Forward to UDP in chunks (UDP MTU ~1400 bytes)
                CHUNK_SIZE = 1024
                if len(data) > CHUNK_SIZE:
                    # Large packet - send in chunks
                    for i in range(0, len(data), CHUNK_SIZE):
                        chunk = data[i:i+CHUNK_SIZE]
                        sock.sendto(chunk, (esp32_ip, udp_port))
                        time.sleep(0.001)  # Small delay between chunks
                else:
                    # Small packet - send as-is
                    sock.sendto(data, (esp32_ip, udp_port))
                
                bytes_to_esp += len(data)
                packets_to_esp += 1
                
                # Show what commands are being sent
                if data[0] == 0x05:
                    print(f"→ ESP32: Dimension query (0x05)")
                elif data[0] == 0x42:
                    print(f"→ ESP32: 16-bit frame command (0x42) - {len(data)} bytes")
                elif data[0] == 0x43:
                    print(f"→ ESP32: 8-bit frame command (0x43) - {len(data)} bytes")
                elif data[0] == 0x06:
                    print(f"→ ESP32: ACK (0x06)")
                else:
                    print(f"→ ESP32: Unknown command (0x{data[0]:02X}) - {len(data)} bytes")
                    if len(data) < 50:
                        print(f"   Content: {repr(data)}")
                
                # Status update
                if packets_to_esp % 50 == 0:
                    print(f"Status: {packets_to_esp} packets, {bytes_to_esp} bytes to ESP32")
            
            # Check for UDP responses (ESP32 → LMCSHD)
            try:
                data, addr = sock.recvfrom(4096)
                if data:
                    # Forward to serial
                    ser.write(data)
                    ser.flush()  # Force write
                    bytes_from_esp += len(data)
                    packets_from_esp += 1
                    print(f"← ESP32: Response {len(data)} bytes (0x{data[0]:02X}) - forwarded to {serial_port}")
                    # Show actual content for dimension responses
                    if len(data) < 20:
                        print(f"   Content: {repr(data)}")
            except:
                pass  # No data available
            
            # Small delay to prevent CPU spinning
            time.sleep(0.00001)
    
    except serial.SerialException as e:
        print(f"\n✗ Serial error: {e}")
        print(f"Make sure {serial_port} exists and is not in use")
        sys.exit(1)
    
    except KeyboardInterrupt:
        print(f"\n\nStopping bridge...")
        print(f"Total → ESP32: {packets_to_esp} packets, {bytes_to_esp} bytes")
        print(f"Total ← ESP32: {packets_from_esp} packets, {bytes_from_esp} bytes")
        ser.close()
        sock.close()
        print("Bridge closed")
    
    except Exception as e:
        print(f"\n✗ Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
