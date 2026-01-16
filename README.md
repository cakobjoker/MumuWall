**MumuWall** 

This project aims to create a large modular display for use in stages in conjunction with other audio-visual equipment. It is made entirely of addressable WS2812B LEDs which can currently accept USB-serial input and function effectively as a monitor for live screen recorder feeds, GIFs, or drawing. I have cloned this repository for WiFi-over-UDP functionality which requires com0com and a python script.

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
* LMCSHD - https://github.com/TylerTimoJ/LMCSHD

**Steps to run**
1. Plug in USB-Serial TTL adapter from PC to board's UART_IN
2. Launch LMCSHD programme:
    * Set port to available COM
    * Set baud rate: 2000000
    * Configure width and height in pixels

**Data Flow**
```LMCSHD -> COM port -> MCU1 UART_IN -> MCU1 UART_OUT -> MCU2 UART_IN```
(Where n is the number of ESPs in the chain)