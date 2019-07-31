#ifdef VSCODE
#include <Arduino.h>
#endif

#include "hardware.h"

#define LIGHT_TYPE DOTSTAR_MATRIX_64x8

// with an older pattern, 52 the battery lasted 4.5 hours. 32 the battery lasted 6 hours
// const uint8_t min_brightness = 22;
const uint8_t min_brightness = 11;
const uint8_t dither_brightness_cutoff = 36; // below this brightness, dithering causes flickering
const uint8_t num_dither_shows = 2; // how many times draw needs to be called to make dithering worthwhile
const uint8_t max_brightness = 255;
const uint8_t visualizer_color_value = 185;
const uint8_t visualizer_white_value = 255;

const uint8_t numLEDsX = 64;
const uint8_t numLEDsY = 8;

// each bin is FREQUENCY_RESOLUTION_HZ (43 Hz with teensy audio shield)
// const uint16_t minBin = 0;
// const uint16_t maxBin = 18000.0 / FREQUENCY_RESOLUTION_HZ + 0.5; // skip over 18kHz

const uint16_t minBin = 0;
const uint16_t maxBin = 18000.0 / FREQUENCY_RESOLUTION_HZ + 0.5; // skip over 18kHz

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
// 200 bpm = 75 ms = 13.333 Hz
// 150 bpm = 100 ms = 10 Hz
// 130 bpm = 115.3 ms = 8.667 Hz
// minimum ms to show a light before allowing it (and sometimes surrounding lights) to change
const uint16_t minOnMs = 200; // 118? 150? 169? 184? 200? 250? 337?
// TODO: round minOnMs to be a multiple of ms_per_frame

// change the pattern every X milliseconds
uint16_t ms_per_shift[] = {
  // maximum speed (no seizure speed)
  // https://www.epilepsy.com/learn/triggers-seizures/photosensitivity-and-seizures
  // "Generally, flashing lights most likely to trigger seizures are between the frequency of 5 to 30 flashes per second (Hertz)."
  // 0.5 is added for rounding up
  uint16_t(1000.0 / 4.0 + 0.5),
  // slow speed
  // 10000,
  // 2000,
  // 42 second rotation
  uint16_t(42 * 1000.0 / float(numLEDsX) + 0.5),
  // ludicrous speed
  26,
  // full throttle
  0,
};

// how close a sound has to be to the loudest sound in order to activate
// TODO: change this. do things with decibles
const float activate_difference = 4.0 / 7.0;
// simple % decrease
// TODO: not sure i like how decay and fade work. i want a more explicit link between this value and how long it takes to fade to black
const float decayMax = 0.995;
// set a floor so that the "maximum" magnitude used as a divisor doesn't go too low
// TODO: tune this with the volume knob?
const float minMaxLevel = 0.26;

// https://github.com/AaronLiddiment/LEDText/wiki/4.Text-Array-&-Special-Character-Codes
// a character is 6 pixels wide. with a 64 pixel screen, we have 10 characters max (64/6 rounded down)
// TODO: proportional font?
// TODO: split this into a bunch of different messages. 2 different woos. and other fun text

// text runs at 11.11fps. so delaying 22 (0x32) frames = 2 seconds
const unsigned char text_flashlight[] = {
  "           "
  EFFECT_RGB "\xff\xff\xff"
  "LIGHT    "
  EFFECT_DELAY_FRAMES "\x00\x16"
  " "
  EFFECT_CUSTOM_RC "\x01"
};

const unsigned char text_woo1[] = {
  "           "
  EFFECT_HSV_AH "\x00\xff\xff\xff\xff\xff" "WOOOOOO!"
};

const unsigned char text_woo2[] = {
  "           "
  EFFECT_HSV "\x00\xff\xff" "W"
  EFFECT_HSV "\x20\xff\xff" "O"
  EFFECT_HSV "\x40\xff\xff" "O"
  EFFECT_HSV "\x60\xff\xff" "O"
  EFFECT_HSV "\xe0\xff\xff" "O"
  EFFECT_HSV "\xc0\xff\xff" "O"
  EFFECT_HSV "\xa0\xff\xff" "O"
  EFFECT_HSV "\x80\xff\xff" "! "
};

const unsigned char text_debug[] = {
  "           "
  EFFECT_HSV_AH "\x00\xff\xff\xff\xff\xff" "A B C D E F G H I J K L M N O P Q R S T U V W X Y Z ! ?"
};

enum ScrollingText {
  none,
  flashlight,
  debug,
  WOO_MESSAGE,
  woo1,
  woo2,
  WOO_MESSAGE_END,
} g_scrolling_text;
