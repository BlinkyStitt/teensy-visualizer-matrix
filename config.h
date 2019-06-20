#ifdef VSCODE
#include <Arduino.h>
#endif

#define VOLUME_KNOB A2
#define SDCARD_CS_PIN 10
#define SPI_MOSI_PIN 7  // alt pin for use with audio board
#define RED_LED 13
#define SPI_SCK_PIN 14  // alt pin for use with audio board
// TODO: pin to check battery level?

// TODO: MATRIX_CS_PIN if we plan on actually using the SD card
#define MATRIX_CLOCK_PIN 0  // yellow wire on my dotstars
#define MATRIX_DATA_PIN 1  // green wire on my dotstars

#define LED_CHIPSET APA102
#define LED_MODE BGR

// TODO: use volume knob for setting brightness. a light sensor could maybe work
#define DEFAULT_BRIGHTNESS 52 // TODO: read from SD. was 52 for 5v leds on the hat. need higher for 3.5v, but lower for being denser

// each frequencyBin = ~43Hz
const uint16_t minBin = 1;   // skip 0-43Hz. it's too noisy
const uint16_t maxBin = 373; // skip over 16kHz

const uint8_t numOutputs = 16; // this needs to fit into a 64 wide matrix
const uint8_t numFreqBands = numOutputs;  // this will grow/shrink to fit inside numOutput. TODO: what should this be? maybe just do 8

const uint8_t numLEDsX = 64;
const uint8_t numLEDsY = 8;

// TODO: make this dynamic. 1, 2, 4 all fit
const uint8_t ledsPerSpreadOutput = 2;
const uint8_t numSpreadOutputs = numOutputs * ledsPerSpreadOutput;
// TODO: make sure numSpreadOutputs fits evenly inside numLEDsX

const uint8_t visualizerNumLEDsX = numSpreadOutputs;
const uint8_t visualizerNumLEDsY = numLEDsY;
// TODO: make sure visualizerNumLEDsX fits evenly inside numSpreadOutputs

const float ms_per_frame = 11.5;  // was 11.5 with less LEDs and a higher bandwidth // 11.5 is as fast as the audio can go

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
const float decayMax = 0.98;  // was .98
// TODO: not sure i like how this works. i want a more explicit link between this value and how long it takes to fade to black
const uint8_t value_visualizer = 255;
const uint8_t fade_rate = 64;
// set a floor so that decayMax doesn't go too low
const float minMaxLevel = 0.10 / activate_difference;
