#define DEBUG
#define DEBUG_SERIAL_WAIT
#include "bs_debug.h"

// TODO: not sure about this
// #define FASTLED_ALLOW_INTERRUPTS 0

#include <stdlib.h>

#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDSprites.h>
#include <LEDText.h>
#include <SD.h>
#include <SPI.h>
#include <SerialFlash.h>
#include <Wire.h>

#define VOLUME_KNOB A2
#define MATRIX_FRONT_CLOCK_PIN 0  // yellow wire on my dotstars
#define MATRIX_FRONT_DATA_PIN 1  // green wire on my dotstars
#define MATRIX_BACK_CLOCK_PIN 2  // yellow wire on my dotstars
#define MATRIX_BACK_DATA_PIN 3  // green wire on my dotstars
#define SDCARD_CS_PIN 10
#define SPI_MOSI_PIN 7  // alt pin for use with audio board
#define RED_LED 13
#define SPI_SCK_PIN 14  // alt pin for use with audio board
// TODO: pin to check battery level?

#define LED_CHIPSET APA102
#define LED_MODE BGR

#define DEFAULT_BRIGHTNESS 52 // TODO: read from SD. was 52 for 5v leds on the hat. need higher for 3.5v, but lower for being denser

const uint16_t numLEDsX = 32;
const uint16_t numLEDsY = 8;

// TODO: not sure if HORIZONTAL_ZIGZAG_MATRIX is actually what we want. we will test when the LEDs arrive
// TODO: we might want negative for Y, but using uint is breaking that
cLEDMatrix<numLEDsX, numLEDsY, VERTICAL_ZIGZAG_MATRIX> leds;

void setupSD() {
  // slave select pin for SPI
  pinMode(SDCARD_CS_PIN, OUTPUT);

  SPI.begin(); // should this be here?

  // read values from the SD card using IniFile
}

void setupLights() {
  // TODO: clock select pin for FastLED to OUTPUT like we do for the SDCARD?
  // TODO: find maximum data rate and then ", DATA_RATE_MHZ(12)"
  FastLED.addLeds<LED_CHIPSET, MATRIX_FRONT_DATA_PIN, MATRIX_FRONT_CLOCK_PIN, LED_MODE, DATA_RATE_MHZ(2)>(leds[0], leds.Size()).setCorrection(TypicalSMD5050);
  // FastLED.addLeds<LED_CHIPSET, MATRIX_BACK_DATA_PIN, MATRIX_BACK_CLOCK_PIN, LED_MODE, DATA_RATE_MHZ(12)>(leds[0], leds.Size()).setCorrection(TypicalSMD5050);

  // TODO: what should this be set to? the flexible panels are much larger
  // led max is 15 amps, but because its flexible, best to keep it max of 5 amps. then we have 2 boards, so multiply by 2
  // FastLED.setMaxPowerInVoltsAndMilliamps(3.7, 5 * 1000 * 2);
  FastLED.setMaxPowerInVoltsAndMilliamps(5.0, 1000);

  FastLED.setBrightness(DEFAULT_BRIGHTNESS); // TODO: read this from the SD card

  FastLED.clear(true);

  Serial.println("Showing red...");
//   FastLED.showColor(CRGB::Red, 127); // this flickers with just 16
  leds(0, 0) = CRGB::Red;
  leds(1, 1) = CRGB::Red;
  leds(2, 2) = CRGB::Red;
  leds(3, 3) = CRGB::Red;
  leds(4, 4) = CRGB::Red;
  leds(5, 5) = CRGB::Red;
  leds(6, 6) = CRGB::Red;
  leds(7, 7) = CRGB::Red;
  leds(8, 7) = CRGB::Red;
  leds(9, 6) = CRGB::Red;
  leds(10, 5) = CRGB::Red;
  leds(11, 4) = CRGB::Red;
  leds(12, 3) = CRGB::Red;
  leds(13, 2) = CRGB::Red;
  leds(14, 1) = CRGB::Red;
  leds(15, 0) = CRGB::Red;
  leds(16, 0) = CRGB::Red;
  leds(17, 1) = CRGB::Red;
  leds(18, 2) = CRGB::Red;
  leds(19, 3) = CRGB::Red;
  leds(20, 4) = CRGB::Red;
  leds(21, 5) = CRGB::Red;
  leds(22, 6) = CRGB::Red;
  leds(23, 7) = CRGB::Red;
  leds(24, 7) = CRGB::Red;
  leds(25, 6) = CRGB::Red;
  leds(26, 5) = CRGB::Red;
  leds(27, 4) = CRGB::Red;
  leds(28, 3) = CRGB::Red;
  leds(29, 2) = CRGB::Red;
  leds(30, 1) = CRGB::Red;
  leds(31, 0) = CRGB::Red;
  FastLED.delay(1000);

  Serial.println("Showing green...");
//   FastLED.showColor(CRGB::Green, 127);
  leds(0, 0) = CRGB::Green;
  leds(1, 1) = CRGB::Green;
  leds(2, 2) = CRGB::Green;
  leds(3, 3) = CRGB::Green;
  leds(4, 4) = CRGB::Green;
  leds(5, 5) = CRGB::Green;
  leds(6, 6) = CRGB::Green;
  leds(7, 7) = CRGB::Green;
  leds(8, 7) = CRGB::Green;
  leds(9, 6) = CRGB::Green;
  leds(10, 5) = CRGB::Green;
  leds(11, 4) = CRGB::Green;
  leds(12, 3) = CRGB::Green;
  leds(13, 2) = CRGB::Green;
  leds(14, 1) = CRGB::Green;
  leds(15, 0) = CRGB::Green;
  leds(16, 0) = CRGB::Green;
  leds(17, 1) = CRGB::Green;
  leds(18, 2) = CRGB::Green;
  leds(19, 3) = CRGB::Green;
  leds(20, 4) = CRGB::Green;
  leds(21, 5) = CRGB::Green;
  leds(22, 6) = CRGB::Green;
  leds(23, 7) = CRGB::Green;
  leds(24, 7) = CRGB::Green;
  leds(25, 6) = CRGB::Green;
  leds(26, 5) = CRGB::Green;
  leds(27, 4) = CRGB::Green;
  leds(28, 3) = CRGB::Green;
  leds(29, 2) = CRGB::Green;
  leds(30, 1) = CRGB::Green;
  leds(31, 0) = CRGB::Green;
  FastLED.delay(1000);

  // TODO: this is not showing
  Serial.println("Showing blue...");
//   FastLED.showColor(CRGB::Blue, 127);
  leds(0, 0) = CRGB::Blue;
  leds(1, 1) = CRGB::Blue;
  leds(2, 2) = CRGB::Blue;
  leds(3, 3) = CRGB::Blue;
  leds(4, 4) = CRGB::Blue;
  leds(5, 5) = CRGB::Blue;
  leds(6, 6) = CRGB::Blue;
  leds(7, 7) = CRGB::Blue;
  leds(8, 7) = CRGB::Blue;
  leds(9, 6) = CRGB::Blue;
  leds(10, 5) = CRGB::Blue;
  leds(11, 4) = CRGB::Blue;
  leds(12, 3) = CRGB::Blue;
  leds(13, 2) = CRGB::Blue;
  leds(14, 1) = CRGB::Blue;
  leds(15, 0) = CRGB::Blue;
  leds(16, 0) = CRGB::Blue;
  leds(17, 1) = CRGB::Blue;
  leds(18, 2) = CRGB::Blue;
  leds(19, 3) = CRGB::Blue;
  leds(20, 4) = CRGB::Blue;
  leds(21, 5) = CRGB::Blue;
  leds(22, 6) = CRGB::Blue;
  leds(23, 7) = CRGB::Blue;
  leds(24, 7) = CRGB::Blue;
  leds(25, 6) = CRGB::Blue;
  leds(26, 5) = CRGB::Blue;
  leds(27, 4) = CRGB::Blue;
  leds(28, 3) = CRGB::Blue;
  leds(29, 2) = CRGB::Blue;
  leds(30, 1) = CRGB::Blue;
  leds(31, 0) = CRGB::Blue;
  FastLED.delay(1000);

  // Serial.println("Showing white...");
  // FastLED.clear(true);
  // leds(0, 0) = CRGB::White;
  // FastLED.delay(500);

//   Serial.println("Clearing lights...");
//   FastLED.clear(true);
  // TODO: END remove this after debug?

  FastLED.show();

  // TODO: turn off onboard LED
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

//   setupAudio();

  Serial.println("Starting...");
}



void loop() {
//   FastLED.show();

  // using FastLED's delay allows for dithering
  // TODO: calculate the delay to get an even framerate now that show takes an uneven amount of time. (14-16ms)
  FastLED.delay(100);
}
