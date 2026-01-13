**MumuWall** 

This project aims to create a large modular display for use in stages in conjunction with other audio-visual equipment. It is made entirely of addressable WS2812B LEDs which can currently accept WiFi-serial or USB-serial input and function effectively as a monitor for live screen recorder feeds, GIFs, or drawing. 

An LED ‘module’ will be standardised to 16x16cm squares to streamline the printing and assembly. For each group of 3x3 modules a 5V 20A switching power supply is to be used. 

The end-goal with this project is to have a stage-sized, weather-resistant wall that can display visuals typically reserved for LCD/OLED displays which are restrictive in shape and modularity, cost prohibitive, and easily damaged.

**Hardware**
* WS2812B LED matrix 16x16 
* Black tint
* DC Barrel Jacks
* 3.5mm AUX Audio Cable for board to board communication
* 5V 20A AC-DC Switching Power Supply
* ESP32 Microcontroller
* Custom cut clear acrylic panels 483x483mm

**Dependencies**
* Python
* pyserial (pip install pyserial)
* LMCSHD - https://github.com/TylerTimoJ/LMCSHD
* COM0COM for virtual serial port

**Steps to run**
1. Connect to the AP ESP32 (only one has the code with AP flag enabled)
2. Connect to the ESP32 WiFi AP
    * Look for network MumuWall_AP
    * Password: mumuwall123
    * AP IP should be 192.168.4.1
3. Start the bridge
    ``` python serial_to_udp_bridge.py COM12 192.168.4.1 ```
4. Configure LMCSHD programme to use COM11:
    * Set port COM11
    * Set baud rate: 2000000
    * Configure width and height in pixels

**Data Flow**
```LMCSHD -> COM11 <--> COM12 -> Bridge script -> UDP -> ESP32 (192.168.4.1:7777) -> nESP32```
(Where n is the number of ESPs in the chain)