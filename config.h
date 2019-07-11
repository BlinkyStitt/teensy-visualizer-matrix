#ifdef VSCODE
#include <Arduino.h>
#endif

#define VOLUME_KNOB A2
#define SDCARD_CS_PIN 10
#define SPI_MOSI_PIN 7  // alt pin for use with audio board
#define RED_LED 13
#define SPI_SCK_PIN 14  // alt pin for use with audio board
// TODO: pin to check battery level?

// APA102 matrix
// TODO: MATRIX_CS_PIN if we plan on actually using the SD card
// with pins 0/1 and 1500kHz data rate, this drew a single frame in 8ms
// with pins 14/7 and 4000kHz data rate, this drew a single frame in 2ms (TODO: double check this)
#define MATRIX_CLOCK_PIN SPI_SCK_PIN  // yellow wire on my dotstars
#define MATRIX_DATA_PIN SPI_MOSI_PIN  // green wire on my dotstars
#define LED_CHIPSET APA102
#define LED_MODE BGR
#define LED_DATA_RATE_KHZ 4000
const float ms_per_frame = 11.5;  // was 11.5 with less LEDs and a higher bandwidth // 11.5 is as fast as the audio can go
const uint8_t numLEDsX = 64;
const uint8_t numLEDsY = 8;

// neopixel matrix
// TODO: make sure FASTLED_ALLOW_INTERRUPTS is 0 when using neopixels
// #define MATRIX_DATA_PIN SPI_MOSI_PIN
// #define LED_CHIPSET NEOPIXEL
// const float ms_per_frame = 11.5;  // was 11.5 with less LEDs and a higher bandwidth // 11.5 is as fast as the audio can go
// // TODO: ms_per_frame this needs to be some multiple of draw_ms
// // TODO: instead of connecting data lines on neopixels, dupe the outputs
// const uint8_t numLEDsX = 32;
// const uint8_t numLEDsY = 8;

// TODO: use volume knob for setting brightness. a light sensor could maybe work
// TODO: or maybe just have a button to toggle between day and night brightness
// 52 the battery lasted 4.5 hours
// 32 the battery lasted 6 hours
#define DEFAULT_BRIGHTNESS 32 // TODO: read from SD. was 52 for 5v leds on the hat. need higher for 3.5v, but lower for being denser

// each frequencyBin = ~43Hz
const uint16_t minBin = 0;   // TODO: skip 0-43Hz by starting at 1? 0 is rather noisy
// const uint16_t maxBin = 372; // skip over 16kHz
const uint16_t maxBin = 418; // skip over 18kHz

const uint8_t numOutputs = 16; // this needs to fit into a 64 wide matrix
const uint8_t numFreqBands = numOutputs;  // this will grow/shrink to fit inside numOutput. TODO: what should this be? maybe just do 8

// TODO: make this dynamic. 1, 2, 4 all fit
const uint8_t ledsPerSpreadOutput = 2;
const uint8_t numSpreadOutputs = numOutputs * ledsPerSpreadOutput;
// TODO: make sure numSpreadOutputs fits evenly inside numLEDsX

const uint8_t visualizerNumLEDsX = numSpreadOutputs;
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
  uint16_t(1.5 * minOnMs / ms_per_frame + 0.5),
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
const float decayMax = 0.98;
// TODO: not sure i like how this works. i want a more explicit link between this value and how long it takes to fade to black
const uint8_t value_visualizer = 255;
const uint8_t fade_rate = 64;
// set a floor so that decayMax doesn't go too low
const float minMaxLevel = 0.15 / activate_difference;
