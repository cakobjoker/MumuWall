**MumuWall** 


This is a giant modular LED wall made for gigs. Each panel comprises of 9 16x16 LED matrices arranged 3x3. The first minimum viable prouct is 2 panels high and 3 wide for use in an upcoming gig, with 3x3 and 3x4 to be worked on later.

Each panel has a dedicated microcontroller.

The control interface is a USB to 3.5mm audio jack from a PC to the first panel's controller. The controller then forwards the draw data to the next panel via 3.5mm audio jack using UART protocol.

**Hardware**
* WS2812B LED matrix 16x16 
* Black tint
* 3.5mm AUX Audio Cable for board to board communication
* 5V 20A AC-DC Switching Power Supply
* ESP32 Microcontroller
* Custom cut clear acrylic panels 490x490mm

**Dependencies**
* LMCSHD - https://github.com/TylerTimoJ/LMCSHD

**How to use the display (if I die or go missing)**
1. Download LMCSHD from github
2. Plug in USB-aux adapter from PC to thee bottom left panel controller's UART_IN
3. Plug the 3 pin connector from the controller to the DIN on the bottom left matrix
4. Connect the UART_OUT of the first controller to the next controller's UART_IN with male-male audio jack
5. Repeat from each UART_OUT to the next panel's UART_IN
6. Follow this snaking pattern for any arrangement of panels (up first column, down the next, up the next, etc):
     * 3  4  9
     * 2  5  8
     * 1  6  7
7. Launch LMCSHD programme:
    * Set port to available COM (there should only be 1, but if there are multiple COM# ports only the correct one will successfully connect)
    * Set baud rate: 2000000 (2 million)
    * Select 8bpp for highest framerate, 16bpp and 24bpp have better colour depth at the expense of framerate
    * Configure width and height in pixels. Each panel is 48x48 pixels, so 2x3 would be 96x144
8. You can now screen record, visualise microphone and speaker audio, or import gifs and images.
