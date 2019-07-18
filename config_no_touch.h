// TODO: what should this file be named?

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
#define FLOATING_PIN 3  // any floating pin. used to seed random
#define VOLUME_KNOB A1
#define SDCARD_CS_PIN 10
#define SPI_MOSI_PIN 7  // alt pin for use with audio board
#define RED_LED 13
#define SPI_SCK_PIN 14  // alt pin for use with audio board
