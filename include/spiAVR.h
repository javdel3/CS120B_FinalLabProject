#ifndef SPIAVR_H
#define SPIAVR_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "helper.h"
#include <avr/pgmspace.h> //Access sprites that are stored in atmega328p program memory

//B5 should always be SCK(spi clock) and B3 should always be MOSI. If you are using an
//SPI peripheral that sends data back to the arduino, you will need to use B4 as the MISO pin.
//The SS pin can be any digital pin on the arduino. Right before sending an 8 bit value with
//the SPI_SEND() funtion, you will need to set your SS pin to low. If you have multiple SPI
//devices, they will share the SCK, MOSI and MISO pins but should have different SS pins.
//To send a value to a specific device, set it's SS pin to low and all other SS pins to high.

// Outputs, pin definitions
#define PIN_SCK                   PORTB5//SHOULD ALWAYS BE B5 ON THE ARDUINO (SCK)
#define PIN_MOSI                  PORTB3//SHOULD ALWAYS BE B3 ON THE ARDUINO (SDA)
#define PIN_SS                    PORTB2 //CS

//ST7735 Specific Pins
#define PIN_RESET                 PORTB1 //Reset (Active Low)
#define PIN_DC                    PORTB0 //A0 (Determines to send DATA (1) or COMMAND(0))
//Note: PORTB4 is MISO so should not be used for my project

//ST7735 Datasheet Commands
char CASET = 0x2A;
char RASET = 0x2B;
char RAMWR = 0x2C;

char SWRESET = 0x01;
char SLPOUT = 0x11;
char COLMOD = 0x3A;
char DISPON = 0x29;


//If SS is on a different port, make sure to change the init to take that into account.
void SPI_INIT(){
    DDRB |= (1 << PIN_SCK) | (1 << PIN_MOSI) | (1 << PIN_SS);//initialize your pins. 
    SPCR |= (1 << SPE) | (1 << MSTR); //initialize SPI coomunication
}


void SPI_SEND(char data)
{
    SPDR = data;//set data that you want to transmit
    while (!(SPSR & (1 << SPIF)));// wait until done transmitting
}

/*
Explanation for Technique of Ex. PORTB |= (1 << PIN_SS);
* PIN_SS are constants that refer to the bit index on the port (NOT the bit itself)
    *Essentially PIN_SS = 2
* 1 shifted by BIT INDEX to the left
    * 1 << PIN_SS
    * = 1 << 2
    * = 0b00000100
* Results in being SET (OR operator) to port's output
    * PORTB |= 0b00000100;
*/

/*
Toggle CS high & low
* CS low = “I am talking to you now.”
* CS high = “Stop listening.”
*/

//ST7735 LCD Functions
void transmit_CMD(char command) {
    PORTB = SetBit(PORTB, PIN_SS, 0); //Set SS low to turn on
    PORTB = SetBit(PORTB, PIN_DC, 0); //Set to recieve commands
    SPI_SEND(command);
    PORTB = SetBit(PORTB, PIN_SS, 1); //Set SS high to turn off
    
}

void transmit_DATA(char data) {
    PORTB = SetBit(PORTB, PIN_SS, 0); //Set SS low to turn on
    PORTB = SetBit(PORTB, PIN_DC, 1); //Set to recieve data
    SPI_SEND(data);
    PORTB = SetBit(PORTB, PIN_SS, 1); //Set SS high to turn off
}

void hardware_RESET() {
    PORTB = SetBit(PORTB, PIN_RESET, 0); //Set RESET low to turn on
    _delay_ms(200); 
    PORTB = SetBit(PORTB, PIN_RESET, 1); //Set RESET high to turn off
    _delay_ms(200);
}

void st7735_INIT() {
    //Hardware reset performed in main
    transmit_CMD(SWRESET);
    _delay_ms(150);
    transmit_CMD(SLPOUT);
    _delay_ms(200);
    transmit_CMD(COLMOD);
    //0x03 (12-bit/pixel)
    //0x05 (16-bit/pixel)
    //0x06 (18-bit/pixel)
    transmit_DATA(0x05);
    _delay_ms(10);
    transmit_CMD(DISPON);
    _delay_ms(200);
    
}

void COL_SET(uint8_t column_start, uint8_t column_end) {
    transmit_CMD(CASET);
    transmit_DATA(0);
    transmit_DATA(column_start);
    transmit_DATA(0);
    transmit_DATA(column_end);
}

void ROW_SET(uint8_t row_start, uint8_t row_end) {
    transmit_CMD(RASET);
    transmit_DATA(0);
    transmit_DATA(row_start);
    transmit_DATA(0);
    transmit_DATA(row_end);
}

//Intended for BGR565 (16 bit color mode w/ BGR (default))
/*
BGR565
high_byte = BBBB BGGG
low_byte  = GGGR RRRR
*/
//Area = (Col_Diff +1) * (Row_Diff + 1)
void MEM_WR(uint8_t blue, uint8_t green, uint8_t red, long int area) {
    transmit_CMD(RAMWR);
    uint8_t high_byte = (blue & 0xF8) | (green >> 5); //Grab top 5 bits of blue & Grab top 3 bits of green
    uint8_t low_byte = ((green & 0x1C) << 3) | (red >> 3); //Grab 3 middle bits of green (excluding top 3 bits & excluding bottom 2 bits) & Grab top 5 bits of red

    for (int i = 0; i < area; i++) {
        //Only works when ST7735 recieves colors in BGR mode in 18 color mode so use same logic for 16 bit color mode
            //Big endian (high first then low second)
        transmit_DATA(high_byte); //Send top 5 bits of blue PLUS 3 most significant bits of green
        transmit_DATA(low_byte); //Send 3 least significant bits of green PLUS top 5 bits of red

        //18 bit color mode
        /*
        transmit_DATA(blue);
        transmit_DATA(green);
        transmit_DATA(red);
        */     
    }
}

//Window that is being modified should be equal to num_pixels (Area Formula)
void DRAW_SPRITE(const uint8_t* byte_array, long int num_pixels) {
    transmit_CMD(RAMWR);
    
    uint8_t read_high_byte;
    uint8_t read_low_byte;

    uint8_t write_high_byte;
    uint8_t write_low_byte;

    //Each pixel = 2 bytes (16 bit color mode) => Reach two elements in arr at a time (2 bytes)
    for (int i = 0; i < num_pixels * 2; i += 2) {
        //total_bytes = num_pixels * 2 
        //Accessing arr index: 0 ... total_bytes - 1
        //Access sprites in program memory by reading the address pointers
        read_high_byte = pgm_read_byte(&byte_array[i]);
        read_low_byte = pgm_read_byte(&byte_array[i+1]);

        //Convert from RGB565 (Byte array) to BG565 (Default for ST7735)
            /*
            RGB: hi = RRRR RGGG & low = GGGB BBBB  
            BGR: hi = BBBB BGGG & low = GGGR RRRR
            */
        unsigned char blue = (read_low_byte & 0x1F) << 3; //XXXB BBBB << 3 => BBBB BXXX
        unsigned char green_hi = (read_high_byte & 0x07); //XXXX XGGG 
        unsigned char green_low = (read_low_byte & 0xE0); // GGGX XXXX
        unsigned char red = (read_high_byte & 0xF8) >> 3; //RRRR RXXX >> 3 => XXXR RRRR

        //Prepare the 2 bytes to be transmitted (16 bit color mode)
        write_high_byte = blue | green_hi;
        write_low_byte = green_low | red;

        //Big endian
        transmit_DATA(write_high_byte); //high byte first 
        transmit_DATA(write_low_byte); //low byte second
    }
}

#endif /* SPIAVR_H */