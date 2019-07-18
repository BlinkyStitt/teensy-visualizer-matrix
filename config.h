#ifdef VSCODE
#include <Arduino.h>
#endif

#include "config_no_touch.h"

#define LIGHT_TYPE DOTSTAR_MATRIX_64x8

#if LIGHT_TYPE == DOTSTAR_MATRIX_64x8
  #pragma message "LIGHT_TYPE = dotstar matrix 2x 32x8"
  // TODO: MATRIX_CS_PIN if we plan on actually using the SD card
  // const float ms_per_frame = 11.5 + 2;  // was 11.5 with less LEDs and a higher bandwidth // 11.5 is as fast as the audio can go
  // const float ms_per_frame = 11.5 + 1.6 + 1.5;  // was 11.5 with less LEDs and a higher bandwidth // 11.5 is as fast as the audio can go, but we need some more time with 4.3ms draw times
  // const float ms_per_frame = 11.5 + 0.25 + 1.35 + 1;  // was 11.5 with less LEDs and a higher bandwidth // added more because its slower to process when it is louder
  // const float ms_per_frame = 1000.0 / 60.0;  // 60 fps. while we can run it faster, that doesn't give us time for dithering
  const float ms_per_frame = 11.485;  // a little slower so we have more time for dither
#elif LIGHT_TYPE == NEOPIXEL_MATRIX_2x_32x8
  #pragma message "LIGHT_TYPE = neopixel matrix 2x 32x8"
  // TODO: maybe this shouldn't be const and we should do (3 * draw_ms + 1) if dither is enabled and draw_ms if it is disabled (min of 11.5 for audio)
  // const float ms_per_frame = 1000.0 / 60.0;  // 11.5 is as fast as the audio can go, but it takes ~9ms to draw and we need multiple draws for dithering
  // const float ms_per_frame = 30;  // 11.5 is as fast as the audio can go, but it takes ~9ms to draw and we need multiple draws for dithering

  // actually, dithering doens't look great when its this slow to draw. it adds a noticeable step to the shift. go back to high speed
  const float ms_per_frame = 11.425;  // 11.5 is as fast as the audio can go, but it takes ~9ms to draw and we need multiple draws for dithering

#else
  #error "unsupported LIGHT_TYPE"
#endif

// with an older pattern, 52 the battery lasted 4.5 hours. 32 the battery lasted 6 hours
const uint8_t min_brightness = 22;
const uint8_t dither_cutoff = 36; // below this brightness, dithering causes flickering
const uint8_t dither_min_shows = 2; // below this brightness, dithering causes flickering
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
