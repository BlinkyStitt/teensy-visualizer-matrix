#ifdef VSCODE
#include <Arduino.h>
#endif

#define VOLUME_KNOB A1
#define SDCARD_CS_PIN 10
#define SPI_MOSI_PIN 7  // alt pin for use with audio board
#define RED_LED 13
#define SPI_SCK_PIN 14  // alt pin for use with audio board
// TODO: pin to check battery level?

// APA102 matrix
// TODO: MATRIX_CS_PIN if we plan on actually using the SD card
// with pins 0/1 and 1500kHz data rate, this drew a single frame in 8ms
// with pins 14/7 and 4000kHz data rate, this drew a single frame in 2ms (TODO: double check this)
// #define MATRIX_CLOCK_PIN SPI_SCK_PIN  // yellow wire on my dotstars
// #define MATRIX_DATA_PIN SPI_MOSI_PIN  // green wire on my dotstars
// #define LED_CHIPSET APA102
// #define LED_MODE BGR
// #define LED_DATA_RATE_KHZ 4000
// // the draw_ms for 512 LEDs is ~4ms
// // const float ms_per_frame = 11.5 + 2;  // was 11.5 with less LEDs and a higher bandwidth // 11.5 is as fast as the audio can go
// const float ms_per_frame = 1000.0 / 60.0;  // 60 fps. while we can run it faster, that doesn't give us time for dithering

// neopixel matrix
// TODO: make sure FASTLED_ALLOW_INTERRUPTS is 0 when using neopixels
// TODO: investigate parallel output
#define MATRIX_DATA_PIN_1 SPI_MOSI_PIN
#define MATRIX_DATA_PIN_2 SPI_SCK_PIN
#define LED_CHIPSET NEOPIXEL
// TODO: maybe this shouldn't be const and we should do (3 * draw_ms + 1) if dither is enabled and draw_ms if it is disabled (min of 11.5 for audio)
const float ms_per_frame = 1000.0 / 30.0;  // 11.5 is as fast as the audio can go, but it takes ~9ms to draw and we need multiple draws for dithering

// 52 the battery lasted 4.5 hours
// 32 the battery lasted 6 hours
const uint8_t min_brightness = 22;
const uint8_t max_brightness = 255;
const uint8_t visualizer_color_value = 185;  // we want 14 (maybe 16) after the balance is done. 
const uint8_t visualizer_white_value = 255;  // we want 22 after the balance is done

const uint8_t numLEDsX = 64;
const uint8_t numLEDsY = 8;

// each frequencyBin = ~43Hz
const uint16_t minBin = 0;   // TODO: skip 0-43Hz by starting at 1? 0 is rather noisy
// const uint16_t maxBin = 372; // skip over 16kHz
const uint16_t maxBin = 418; // skip over 18kHz

// TODO: make this configurable while the program is running
const uint8_t numFreqBands = 16;  // this needs to fit into a 64 wide matrix

// TODO: cycle between multiple patterns
const uint8_t visualizerXtoFrequencyId[] = {
   0, // 0
  99, // 1 (>numFreqBands == OFF)
   1, // 2
  99, // 3
   2, // 4
  99, // 5
   3, // 6
  99, // 7
   4, // 8
  99, // 9
   5, // 10
  99, // 11
   6, // 12
  99, // 13
   7, // 14
  99, // 15
   8, // 16
  99, // 17
   9, // 18
  99, // 19
  10, // 20
  99, // 21
  11, // 22
  99, // 23
  12, // 24
  99, // 25
  13, // 26
  99, // 27
  14, // 28
  99, // 29
  15, // 30
  99, // 31
};
// const uint8_t visualizerXtoFrequencyId[] = {
//   0,  // 0
//   0,  // 1
//   99, // 2 (>numFreqBands == OFF)
//   99, // 3
//   1,  // 4
//   1,  // 5
//   99, // 6
//   99, // 7
//   2,  // 8
//   2,  // 9
//   99, // 10
//   99, // 11
//   3,  // 12
//   3,  // 13
//   99, // 14
//   99, // 15
//   4,  // 16
//   4,  // 17
//   99, // 18
//   99, // 19
//   5,  // 20
//   5,  // 21
//   99, // 22
//   99, // 23
//   6,  // 24
//   6,  // 25
//   99, // 26
//   99, // 27
//   7,  // 28
//   7,  // 29
//   99, // 30
//   99, // 31
// };

const uint8_t visualizerNumLEDsX = 32;  // TODO: put this in a struct with frequencyToVisualizer?
const uint8_t visualizerNumLEDsY = numLEDsY;
// TODO: make sure visualizerNumLEDsX fits evenly inside numSpreadOutputs

// the shortest amount of time to leave an output on before starting to change it
// it will stay on longer than this depending on time required to dim to off
// https://www.epilepsy.com/learn/triggers-seizures/photosensitivity-and-seizures
// "Generally, flashing lights most likely to trigger seizures are between the frequency of 5 to 30 flashes per second (Hertz)."
// 0.5 is added for rounding up
const uint16_t minOnMs = 1000.0 / 4.0 + 0.5; // 118? 150? 169? 184? 200? 250? 337?
// TODO: round minOnMs to be a multiple of ms_per_frame

// slide the leds over 1 every X frames
float seconds_for_slow_rotation = 42;
uint16_t frames_per_shift[] = {
  // maximum speed (no seizure speed)
  uint16_t(minOnMs / ms_per_frame + 0.5),
  // slow speed
  uint16_t(seconds_for_slow_rotation * 1000.0 / float(numLEDsX) / ms_per_frame + 0.5),
  // ludicrous speed
  3,
  // full throttle
  1,
};

// how close a sound has to be to the loudest sound in order to activate
const float activate_difference = 2.5 / 6.0;
// simple % decrease
// TODO: not sure i like how decay and fade work. i want a more explicit link between this value and how long it takes to fade to black
const float decayMax = 0.98;
const uint8_t fade_rate = 64;
// set a floor so that decayMax doesn't go too low
const float minMaxLevel = 0.15 / activate_difference;
