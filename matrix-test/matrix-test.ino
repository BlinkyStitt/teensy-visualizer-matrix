#define DEBUG
#define DEBUG_SERIAL_WAIT
#include "bs_debug.h"

#include <stdlib.h>

#include <Audio.h>
#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDSprites.h>
#include <LEDText.h>
#include <SD.h>
#include <SPI.h>
#include <SerialFlash.h>
#include <Wire.h>

#define VOLUME_KNOB A2
#define SDCARD_CS_PIN 10
#define SPI_MOSI_PIN 7  // alt pin for use with audio board (which is using 11)
#define RED_LED 13
#define SPI_SCK_PIN 14  // alt pin for use with audio board (which is using 13)
// TODO: pin to check battery level?

// TODO: software spi is causing problems. switch to hardware pins (which we only have one of!)
#define MATRIX_CLOCK_PIN SPI_MOSI_PIN  // yellow wire on my dotstars
#define MATRIX_DATA_PIN SPI_SCK_PIN  // green wire on my dotstars
// TODO: MATRIX_CS_PIN and hardware so that we can access the SD without making the lights go crazy

#define LED_CHIPSET APA102
#define LED_MODE BGR

#define DEFAULT_BRIGHTNESS 25 // TODO: read from SD. was 52 for 5v leds on the hat. need higher for 3.5v, but lower for being denser

AudioInputI2S i2s1;  // xy=139,91
AudioOutputI2S i2s2; // xy=392,32
AudioAnalyzeFFT1024 fft1024;
AudioConnection patchCord1(i2s1, 0, i2s2, 0);
AudioConnection patchCord2(i2s1, 0, fft1024, 0);
AudioControlSGTL5000 audioShield; // xy=366,225

const uint16_t numLEDsX = 64;
const uint16_t numLEDsY = 8;

// TODO: not sure if HORIZONTAL_ZIGZAG_MATRIX is actually what we want. we will test when the LEDs arrive
// TODO: we might want negative for Y, but using uint16_t is breaking that
cLEDMatrix<numLEDsX, numLEDsY, VERTICAL_ZIGZAG_MATRIX> leds;

void setupSD() {
  // slave select pin for SPI
  pinMode(SDCARD_CS_PIN, OUTPUT);

  SPI.begin(); // should this be here?

  // read values from the SD card using IniFile
}

void setupAudio() {
  // Audio requires memory to work. I haven't seen this go over 11
  AudioMemory(12);

  // Enable the audio shield and set the output volume.
  audioShield.enable();
  audioShield.muteHeadphone(); // to avoid any clicks
  audioShield.inputSelect(AUDIO_INPUT_MIC);
  audioShield.volume(0.5);
  audioShield.micGain(60); // was 63, then 40  // 0-63 // TODO: tune this

  //audioShield.audioPreProcessorEnable(); // todo: pre or post?

  // bass, mid_bass, midrange, mid_treble, treble
  // TODO: tune this. maybe read from SD card
  //audioShield.eqSelect(GRAPHIC_EQUALIZER);
  // audioShield.eqBands(-0.80, -0.75, -0.50, 0.50, 0.80);  // the great northern
  // audioShield.eqBands(-0.5, -.2, 0, .2, .5);  // todo: tune this
  // audioShield.eqBands(-0.80, -0.10, 0, 0.10, 0.33);  // todo: tune this
  //audioShield.eqBands(0.0, 0.0, 0.0, 0.1, 0.33); // todo: tune this

  audioShield.unmuteHeadphone(); // for debugging
}

void setupLights() {
  // TODO: turn off onboard LED

  // TODO: clock select pin for FastLED to OUTPUT like we do for the SDCARD?
  // TODO: find maximum data rate and then ", DATA_RATE_MHZ(12)"
  FastLED.addLeds<LED_CHIPSET, SPI_MOSI_PIN, SPI_SCK_PIN, LED_MODE, DATA_RATE_KHZ(800)>(leds[0], leds.Size()).setCorrection(TypicalSMD5050);

  // TODO: what should this be set to? the flexible panels are much larger
  // led max is 15 amps, but because its flexible, best to keep it max of 5 amps. then we have 2 boards, so multiply by 2
  // FastLED.setMaxPowerInVoltsAndMilliamps(3.7, 5 * 1000 * 2);
  FastLED.setMaxPowerInVoltsAndMilliamps(5.0, 500);

  FastLED.setBrightness(DEFAULT_BRIGHTNESS); // TODO: read this from the SD card

  FastLED.clear(true);
}

void setup() {
  debug_serial(115200, 2000);

  Serial.println("Setting up...");

  // setup SPI for the Audio board (which has an SD card reader)
  SPI.setMOSI(SPI_MOSI_PIN);
  SPI.setSCK(SPI_SCK_PIN);

  setupSD();

  // TODO: read SD card here to configure things

  // TODO: read numOutputs from the SD card

  setupLights();

  // setupAudio();

  Serial.println("Starting...");
}

void colorPattern(CRGB::HTMLColorCode color) {
  uint16_t y = 0;
  for (uint16_t x = 0; x < numLEDsX; x++) {
    y = x % numLEDsY;
    leds(x, y) = color;

    y = (x + 2) % numLEDsY;
    leds(x, y) = color;

    // y = (x + 4) % numLEDsY;
    // if (y % 2 == 0) {
    //   leds(x, y) = color;
    // }
  }
}

void loop() {
  Serial.println("Showing red...");
  colorPattern(CRGB::Red);
  FastLED.delay(2000);
  // FastLED.show();
  // delay(2000);

  Serial.println("Showing green...");
  colorPattern(CRGB::Green);
  FastLED.delay(2000);
  // FastLED.show();
  // delay(2000);

  Serial.println("Showing blue...");
  colorPattern(CRGB::Blue);
  FastLED.delay(2000);
  // FastLED.show();
  // delay(2000);
}
