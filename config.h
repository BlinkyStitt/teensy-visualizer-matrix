#ifdef VSCODE
#include <Arduino.h>
#endif

// you can change me!
// comment INPUT_TOUCH out to disable the touch sensor
// #define INPUT_TOUCH
//

#include "hardware.h"

// you can change me!
#define LIGHT_TYPE EL_WIRE_8
//

// don't change me
#if LIGHT_TYPE == DOTSTAR_MATRIX_64x8
  #pragma message "LIGHT_TYPE = dotstar matrix 2x 32x8"
  // TODO: MATRIX_CS_PIN if we plan on actually using the SD card

  #define OUTPUT_LED
  #define OUTPUT_LED_MATRIX
#elif LIGHT_TYPE == NEOPIXEL_MATRIX_2x_32x8
  #pragma message "LIGHT_TYPE = neopixel matrix 2x 32x8"

  #define OUTPUT_LED
  #define OUTPUT_LED_MATRIX
#elif LIGHT_TYPE == EL_WIRE_8
  #pragma message "LIGHT_TYPE = EL Wire x8"
#elif LIGHT_TYPE == DOTSTAR_STRIP_120
  #pragma message "LIGHT_TYPE = dotstar strip 120"

  #define OUTPUT_LED

  #error "WIP"
#else
  #error "unsupported LIGHT_TYPE"
#endif
// END don't change me

// with an older pattern, 52 the battery lasted 4.5 hours. 32 the battery lasted 6 hours
// const uint8_t min_brightness = 22;
const uint8_t min_brightness = 11;
const uint8_t dither_brightness_cutoff = 36; // below this brightness, dithering causes flickering
const uint8_t num_dither_shows = 2; // how many times draw needs to be called to make dithering worthwhile
const uint8_t max_brightness = 255;
const uint8_t visualizer_color_value = 185;
const uint8_t visualizer_white_value = 255;

#ifdef OUTPUT_LED_MATRIX
  const uint8_t numLEDsX = 64;
  const uint8_t numLEDsY = 8;
#elif LIGHT_TYPE == EL_WIRE_8
  // 
#else
  #error WIP
#endif

// each bin is FREQUENCY_RESOLUTION_HZ (43 Hz with teensy audio shield)
const uint16_t minBin = 0;
const uint16_t maxBin = 18000.0 / FREQUENCY_RESOLUTION_HZ + 0.5; // skip over 18kHz

// TODO: make this configurable while the program is running?
#ifdef OUTPUT_LED_MATRIX
  const uint8_t numFreqBands = 16;  // this needs to fit into a 64 wide matrix
#elif defined OUTPUT_LED
  const uint8_t numFreqBands = 11;

  const int maxOn = numOutputs * 3 / 4;
#elif LIGHT_TYPE == EL_WIRE_8
  const uint8_t numFreqBands = 8;

  // we don't want all the lights to be on at once
  const int maxOn = 5;
#else
  #error WIP
#endif

#ifdef OUTPUT_LED_MATRIX
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

#endif

#ifdef OUTPUT_LED_MATRIX
  // the shortest amount of time to leave an output on before starting to change it
  // it will stay on longer than this depending on time required to dim to off
  // 200 bpm = 75 ms = 13.333 Hz
  // 150 bpm = 100 ms = 10 Hz
  // 130 bpm = 115.3 ms = 8.667 Hz
  // minimum ms to show a light before allowing it (and sometimes surrounding lights) to change
  const uint16_t minOnMs = 200; // 118? 150? 169? 184? 200? 250? 337?
  // TODO: round minOnMs to be a multiple of ms_per_frame
#elif defined OUTPUT_LED
  // TODO: tune this now that we track the sound differently
  const unsigned int minOnMs = 337; // 118? 150? 184? 200? 250?
#elif LIGHT_TYPE == EL_WIRE_8
  const uint16_t minOnMs = 250;  // TODO: tune this
#else
  #error WIP
#endif

#ifdef OUTPUT_LED
  // change the pattern every X milliseconds
  uint16_t ms_per_shift[] = {
    // maximum speed (no seizure speed)
    // https://www.epilepsy.com/learn/triggers-seizures/photosensitivity-and-seizures
    // "Generally, flashing lights most likely to trigger seizures are between the frequency of 5 to 30 flashes per second (Hertz)."
    // 0.5 is added for rounding up
    uint16_t(1000.0f / 4.0f + 0.5f),
    // slow speed
    // 10000,
    // 2000,
    // 42 second rotation
    uint16_t(42.0f * 1000.0f / float(numLEDsX) + 0.5f),
    // ludicrous speed
    26,
    // full throttle
    0,
  };
#endif

#ifdef OUTPUT_LED_MATRIX
  // how close a sound has to be to the loudest sound in order to activate
  // TODO: change this. do things with log scale
  const float activate_difference = 4.0f / 7.0f;
#elif defined OUTPUT_LED
  // TODO: tune this
  const float activate_difference = 0.85f;
#elif LIGHT_TYPE == EL_WIRE_8
  // TODO: tune this
  const float activate_difference = 0.5f;
#else
  #error WIP
#endif

// simple % decrease
// TODO: not sure i like how decay and fade work. i want a more explicit link between this value and how long it takes to fade to black
const float decayMax = 0.995f;
// set a floor so that the "maximum" magnitude used as a divisor doesn't go too low
// TODO: tune this with the volume knob?
const float minMaxLevel = 0.26f;

#ifdef OUTPUT_LED_MATRIX
  // https://github.com/AaronLiddiment/LEDText/wiki/4.Text-Array-&-Special-Character-Codes
  // a space character is 8 pixels wide. with a 64 pixel screen, we need 8 spaces to clear the screen
  // TODO: proportional font?
  // TODO: split this into a bunch of different messages. 2 different woos. and other fun text

  // text runs at 11.11fps. so delaying 22 (0x32) frames = 2 seconds
  const unsigned char text_flashlight[] = {
    "       "
    EFFECT_RGB "\xff\xff\xff"
    "LIGHT "
    EFFECT_CUSTOM_RC "\x01"
  };

  const unsigned char text_woo1[] = {
    "        "
    EFFECT_HSV_AH "\x00\xff\xff\xff\xff\xff" "WOOOOOO! "
  };

  const unsigned char text_woo2[] = {
    "        "
    EFFECT_HSV "\x00\xff\xff" "W"
    EFFECT_HSV "\x20\xff\xff" "O"
    EFFECT_HSV "\x40\xff\xff" "O"
    EFFECT_HSV "\x60\xff\xff" "O"
    EFFECT_HSV "\xe0\xff\xff" "O"
    EFFECT_HSV "\xc0\xff\xff" "O"
    EFFECT_HSV "\xa0\xff\xff" "O"
    EFFECT_HSV "\x80\xff\xff" "! "
  };

  const unsigned char text_party[] = {
    "        "
    EFFECT_HSV_AH "\x00\xff\xff\xff\xff\xff" "PARTY! "
  };

  const unsigned char text_dance[] = {
    "        "
    EFFECT_HSV_AH "\x00\xff\xff\xff\xff\xff" "DANCE! "
  };

  const unsigned char text_gambino[] = {
    "        "
    EFFECT_HSV_AH "\x00\xff\xff\xff\xff\xff" "CHILDISH GAMBINO! "
  };

  const unsigned char text_debug[] = {
    "        "
    EFFECT_HSV_AH "\x00\xff\xff\xff\xff\xff"
    "HI THERE! "
    // "A B C D E F G H I J K L M N O P Q R S T U V W X Y Z "
    // "0 1 2 3 4 5 6 7 8 9 "
    // "! \" # $ % & '( ) * + , - . / "
    // ": ; < = > ? @ "
    // "[ \\ ] ^ _ ` "
  };

  enum ScrollingText {
    none,
    flashlight,
    debug,
    CHEER,
    woo1,
    gambino,
    woo2,
    party,
    dance,
    CHEER_END,
  } g_scrolling_text;
#endif
