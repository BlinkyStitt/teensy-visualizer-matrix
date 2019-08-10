#ifndef _BV
#define _BV(bit) (1 << (bit)) 
#endif

// enum LIGHT_TYPES {
//   DS_MATRIX_2x_32x8=0,
//   NP_MATRIX_2x_32x8=1,
//   // EL_WIRE_8x=2
//   // DOTSTAR_STRIP_120=3
// };
// TODO: wtf. why does the above not work?

#define DOTSTAR_MATRIX_64x8 0
#define NEOPIXEL_MATRIX_2x_32x8 1

// Teensy 3.2 w/ Audio board
// TODO: document more pins. audio: 9, 11, 13, 18, 19, 22, 23. 
#define FLOATING_PIN 3  // any floating pin. used to seed random
#define SPI_MOSI_PIN 7  // alt pin for use with audio board
#define SDCARD_CS_PIN 10
#define SPI_MISO_PIN 12
#define RED_LED 13  // this is also used by the audio board's sgtl5000 RX line
#define SPI_SCK_PIN 14  // alt pin for use with audio board
#define VOLUME_KNOB 15  // A1

// IRQ is pulled up to 3.3V on the breakout
// when the sensor chip detects a change in the touch sense switches, the pin goes to 0V until the data is read over i2c
#define MPR121_IRQ 16 // TODO: is this a good pin for this?

// these can be shared between multiple i2c devices
#define SDA 18
#define SDC 19

// Default address is 0x5A, if tied to 3.3V its 0x5B
// If tied to SDA its 0x5C and if SCL then 0x5D
#define MPR121_ADDRESS 0x5A

#define FREQUENCY_RESOLUTION_HZ 43

enum touches {
  brim_front = 0,
  top = 2,
  brim_left = 4,  // blue
  brim_right = 6,  // green
};
