#!/usr/bin/env python3
"""
Direct UDP test - sends test frame to ESP32 bypassing LMCSHD
"""

import socket
import time

WIDTH = 48
HEIGHT = 48
ESP32_IP = "192.168.4.1"
UDP_PORT = 7777

print("=== Direct UDP Test to ESP32 ===")
print(f"Target: {ESP32_IP}:{UDP_PORT}")
print(f"Size: {WIDTH}x{HEIGHT}\n")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(2.0)
sock.bind(('', 0))

# Test 1: Dimension query
print("Test 1: Dimension query (0x05)")
sock.sendto(bytes([0x05]), (ESP32_IP, UDP_PORT))

try:
    data, addr = sock.recvfrom(1024)
    print(f"✓ Response: {repr(data)}")
except:
    print("✗ No response (timeout)")

time.sleep(0.5)

# Test 2: Send gradient frame
print("\nTest 2: Sending gradient frame (0x43)")

# Create gradient pattern
frame = bytearray()
for y in range(HEIGHT):
    for x in range(WIDTH):
        # Red to blue gradient
        r = (x * 7) // WIDTH  # 0-7 for RRR
        b = ((WIDTH - x) * 3) // WIDTH  # 0-3 for BB
        pixel = (r << 5) | b  # RRRGGG00 + BB
        frame.append(pixel)

print(f"Frame size: {len(frame)} bytes")

# Send 8-bit frame command first
print("\nSending 0x43 (8-bit frame) command...")
sock.sendto(bytes([0x43]), (ESP32_IP, UDP_PORT))
time.sleep(0.01)  # Small delay

# Send frame data in chunks (UDP MTU limit ~1400 bytes)
CHUNK_SIZE = 1024
total_sent = 0
chunks = (len(frame) + CHUNK_SIZE - 1) // CHUNK_SIZE
print(f"Sending {len(frame)} bytes in {chunks} chunks of {CHUNK_SIZE} bytes...")

for i in range(0, len(frame), CHUNK_SIZE):
    chunk = frame[i:i+CHUNK_SIZE]
    sock.sendto(bytes(chunk), (ESP32_IP, UDP_PORT))
    total_sent += len(chunk)
    print(f"  Sent chunk {i//CHUNK_SIZE + 1}/{chunks}: {len(chunk)} bytes (total: {total_sent})")
    time.sleep(0.005)  # Small delay between chunks

print(f"Frame data sent: {total_sent} bytes")

# Wait for ACK
print("\nWaiting for ACK...")
try:
    data, addr = sock.recvfrom(1024)
    if len(data) > 0 and data[0] == 0x06:
        print("✓ ACK received! Check LEDs for gradient pattern")
    else:
        print(f"? Unexpected: {repr(data)}")
except:
    print("✗ No ACK (timeout)")

sock.close()
print("\nIf you see a red-to-blue gradient on the LEDs, ESP32 is working perfectly!")
