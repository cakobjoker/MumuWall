#!/usr/bin/env python3
"""
Serial to UDP Bridge for MumuWall
Forwards data from LMCSHD (or any serial tool) to ESP32 WiFi AP via UDP

Usage:
    python serial_to_udp_bridge.py COM3 192.168.4.1
    
Where COM3 is your virtual serial port and 192.168.4.1 is the ESP32 AP IP
"""

import socket
import sys
import serial
import time

def main():
    if len(sys.argv) < 3:
        print("Usage: python serial_to_udp_bridge.py <SERIAL_PORT> <ESP32_IP>")
        print("Example: python serial_to_udp_bridge.py COM3 192.168.4.1")
        sys.exit(1)
    
    serial_port = sys.argv[1]
    esp32_ip = sys.argv[2]
    udp_port = 7777
    baud_rate = 2000000  # Match ESP32 baud rate
    
    print(f"=== Serial to UDP Bridge ===")
    print(f"Serial Port: {serial_port} @ {baud_rate} baud")
    print(f"UDP Target: {esp32_ip}:{udp_port}")
    print(f"Connecting...\n")
    
    try:
        # Open serial port
        ser = serial.Serial(serial_port, baud_rate, timeout=0.1)
        print(f"✓ Serial port opened")
        
        # Create UDP socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        print(f"✓ UDP socket created")
        print(f"\nBridge active! Forwarding serial → UDP")
        print(f"Press Ctrl+C to stop\n")
        
        bytes_forwarded = 0
        packets_sent = 0
        
        while True:
            # Read from serial
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                
                # Forward to UDP
                sock.sendto(data, (esp32_ip, udp_port))
                
                bytes_forwarded += len(data)
                packets_sent += 1
                
                # Status update every 100 packets
                if packets_sent % 100 == 0:
                    print(f"Forwarded: {packets_sent} packets, {bytes_forwarded} bytes")
            
            # Small delay to prevent CPU spinning
            time.sleep(0.001)
    
    except serial.SerialException as e:
        print(f"\n✗ Serial error: {e}")
        print(f"Make sure {serial_port} exists and is not in use")
        sys.exit(1)
    
    except KeyboardInterrupt:
        print(f"\n\nStopping bridge...")
        print(f"Total: {packets_sent} packets, {bytes_forwarded} bytes forwarded")
        ser.close()
        sock.close()
        print("Bridge closed")
    
    except Exception as e:
        print(f"\n✗ Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
