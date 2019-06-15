#define DEBUG
#define DEBUG_SERIAL_WAIT
#include "bs_debug.h"

// TODO: not sure about this
// #define FASTLED_ALLOW_INTERRUPTS 0

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
const uint minBin = 1;   // skip 0-43Hz. it's too noisy
const uint maxBin = 373; // skip over 16kHz

const uint numOutputs = 8; // this needs to fit into a 64 wide matrix
const uint numFreqBands = numOutputs;  // this will grow/shrink to fit inside numOutput. TODO: what should this be? maybe just do 8

// the shortest amount of time to leave an output on
// TODO: tune this!
const uint minOnMs = 100; // 118? 150? 184? 200? 250? 337?

const uint16_t numLEDsX = 64; // TODO: might need to drop this to 32 and have 2 sets of pins
const uint16_t numLEDsY = 8;

// TODO: spread used to mean turn multiple on. now i want it to be a gap
const uint ledsPerSpreadOutput = 2;
const uint numSpreadOutputs = numOutputs * ledsPerSpreadOutput;
// TODO: make sure numSpreadOutputs fits evenly inside numLEDsX

const uint16_t visualizerNumLEDsX = numSpreadOutputs;
const uint16_t visualizerNumLEDsY = numLEDsY; // this is one way to keep power down

// slide the leds over 1 every X frames
// TODO: tune this now that the LEDs are denser. this might be way too fast
const float seconds_for_full_rotation = 66.6;
const float ms_per_frame = 11.5;  // was 11.5 with less LEDs and a higher bandwidth // 11.5 is as fast as the audio can go
// 0.5 is added for rounding up
const uint frames_per_shift = uint(seconds_for_full_rotation * 1000.0 / (2.0 * numLEDsX) / float(ms_per_frame) + 0.5);

// how close a sound has to be to the loudest sound in order to activate
// TODO: i think we should change this now that we have a y-axis to use. lower this to like 33% and have the current, neighbor, max volumes always involved
const float activate_difference = 0.80;
// simple % decrease
const float decayMax = 0.99;  // was 98
// set a floor so that decayMax doesn't go too low
const float minMaxLevel = 0.15 / activate_difference;

// how much of the neighbor's max to consider when deciding when to turn on
const float scale_neighbor_max = 0.9;
// how much of all the other bin's max to consider when deciding when to turn on
const float scale_overall_max = 0.616;
// how quickly to fade to black
const uint value_min = 25;
const uint fade_factor = 4;  // was 16 on the hat. TODO: calculate this based on the framerate and a time to go from max to 0.

// TODO: make sure visualizerNumLEDsX fits evenly inside numSpreadOutputs

uint freqBands[numFreqBands];
CHSV frequencyColors[numFreqBands];

// frequencyColors are stretched/squished to fit this (squishing being what you probably want)
// TODO: rename this to something more descriptive. or maybe get rid of it and store directly on the visualizer_matrix
CHSV outputs[numOutputs];

// outputs are stretched to fit this
// TODO: rename this to something more descriptive. or maybe get rid of it and store directly on the visualizer_matrix
CHSV outputsStretched[numSpreadOutputs];

// TODO: not sure if HORIZONTAL_ZIGZAG_MATRIX is actually what we want. we will test when the LEDs arrive
// TODO: we might want negative for Y, but using uint is breaking that
cLEDMatrix<-visualizerNumLEDsX, visualizerNumLEDsY, VERTICAL_ZIGZAG_MATRIX> visualizer_matrix;

// because of how we fade the visualizer slowly, we need to have a seperate matrix for the sprites and text
cLEDMatrix<numLEDsX, numLEDsY, VERTICAL_ZIGZAG_MATRIX> sprite_matrix;
cLEDSprites Sprites(&sprite_matrix);
cLEDText ScrollingMsg;

// the sprites and visualizer get combined into this
cLEDMatrix<numLEDsX, numLEDsY, VERTICAL_ZIGZAG_MATRIX> leds;

AudioInputI2S i2s1;  // xy=139,91
AudioOutputI2S i2s2; // xy=392,32
AudioAnalyzeFFT1024 fft1024;
AudioConnection patchCord1(i2s1, 0, i2s2, 0);
AudioConnection patchCord2(i2s1, 0, fft1024, 0);
AudioControlSGTL5000 audioShield; // xy=366,225

// we don't want all the levels to be on at once
// TODO: change this now that we are connected to a matrix
const uint maxOn = numOutputs * 3 / 4;
uint numOn = 0;

// keep track of the max volume for each frequency band (slowly decays)
float maxLevel[numFreqBands];
// keep track of the current levels. this is a sum of multiple frequency bins.
float currentLevel[numFreqBands];

// going through the levels loudest to quietest makes it so we can ensure the loudest get turned on ASAP
int sortedLevelIndex[numFreqBands];

// keep track of when to turn lights off so they don't flicker
unsigned long turnOffMsArray[numFreqBands];

// used to keep track of framerate // TODO: remove this if debug mode is disabled
unsigned long lastUpdate = 0;
unsigned long lastDraw = 0;

/* sort the levels normalized against their max
 *
 * with help from https://phoxis.org/2012/07/12/get-sorted-index-orderting-of-an-array/
 */
static int compare_levels(const void *a, const void *b) {
  // TODO: check for uint to int overflow issues
  int aa = *((int *)a), bb = *((int *)b);
  return (currentLevel[bb] / maxLevel[bb]) - (currentLevel[aa] / maxLevel[aa]);
}

float FindE(uint bands, uint minBin, uint maxBin) {
  // https://forum.pjrc.com/threads/32677-Is-there-a-logarithmic-function-for-FFT-bin-selection-for-any-given-of-bands?p=133842&viewfull=1#post133842
  float increment = 0.1, eTest, n;
  uint b, count, d;

  for (eTest = 1; eTest < maxBin; eTest += increment) { // Find E through brute force calculations
    count = minBin;
    for (b = 0; b < bands; b++) { // Calculate full log values
      n = pow(eTest, b);
      d = uint(n + 0.5);
      count += d;
    }

    if (count > maxBin) { // We calculated over our last bin
      eTest -= increment; // Revert back to previous calculation increment
      increment /= 10.0;  // Get a finer detailed calculation & increment a decimal point lower

      if (increment < 0.0000001) { // Ran out of calculations. Return previous E. Last bin will be lower than (bins-1)
        return (eTest - increment);
      }
    } else if (count == maxBin) { // We found the correct E
      return eTest;               // Return calculated E
    }
  }

  return 0; // Return error 0
}

void setupFFTBins() {
  // https://forum.pjrc.com/threads/32677-Is-there-a-logarithmic-function-for-FFT-bin-selection-for-any-given-of-bands?p=133842&viewfull=1#post133842
  float e, n;
  uint count = minBin, d;

  e = FindE(numFreqBands, minBin, maxBin); // Find calculated E value

  if (e) {                           // If a value was returned continue
    Serial.printf("E = %4.4f\n", e); // Print calculated E value
    Serial.printf("  i  low high\n");
    for (uint b = 0; b < numFreqBands; b++) { // Test and print the bins from the calculated E
      n = pow(e, b);
      d = int(n + 0.5);

      Serial.printf("%3d ", b);

      Serial.printf("%4d ", count); // Print low bin
      freqBands[b] = count;

      count += d - 1;
      Serial.printf("%4d\n", count); // Print high bin

      count++;
    }
  } else {
    Serial.println("Error calculating E"); // Error, something happened
    while (1)
      ;
  }
}

void setupSD() {
  // slave select pin for SPI
  pinMode(SDCARD_CS_PIN, OUTPUT);

  // read values from the SD card using IniFile
}

void colorPattern(CRGB::HTMLColorCode color) {
  for (uint16_t x = 0; x < numLEDsX; x++) {
    uint16_t y = x % numLEDsY;
    leds(x, y) = color;
  }
}

unsigned long draw_ms = 10;

void setupLights() {
  // TODO: turn off onboard LED
  // TODO: clock select pin for FastLED to OUTPUT like we do for the SDCARD?

  // with software spi, for one panel, 2mhz worked. when a second panel was added, 2mhz crashed after a few seconds, but 1mhz is working on my test code. crashed after a second or so though 
  // looks like 500 mhz can run 2 panels, but we are having power troubles now. more power might mean we can increase the rate
  FastLED.addLeds<LED_CHIPSET, MATRIX_DATA_PIN, MATRIX_CLOCK_PIN, LED_MODE, DATA_RATE_KHZ(1500)>(leds[0], leds.Size()).setCorrection(TypicalSMD5050);

  // TODO: what should this be set to? the flexible panels are much larger
  // led matrix max is 15 amps, but because its flexible, best to keep it max of 5 amps. then we have 2 boards, so multiply by 2
  // FastLED.setMaxPowerInVoltsAndMilliamps(3.7, 5 * 1000 * 2);
  FastLED.setMaxPowerInVoltsAndMilliamps(5.0, 180); // when running through teensy's usb port, the max draw is much lower than with a battery

  FastLED.setBrightness(DEFAULT_BRIGHTNESS); // TODO: read this from the SD card. or allow tuning with the volume knob

  // clear all the arrays
  for (uint i = 0; i < numFreqBands; i++) {
    frequencyColors[i].value = 0;
  }
  for (uint i = 0; i < numOutputs; i++) {
    outputs[i].value = 0;
  }
  for (uint i = 0; i < numSpreadOutputs; i++) {
    outputsStretched[i].value = 0;
  }

  FastLED.clear(true);

  Serial.println("Showing red...");
  colorPattern(CRGB::Red);
  // TODO: fastled.delay is sending refreshes too quickly and crashing
  // FastLED.show();
  // delay(1500);

  // time FastLED.show so we can delay the right amount in our main loop
  draw_ms = millis();
  FastLED.show();
  draw_ms = millis() - draw_ms;

  Serial.print("Draw time: ");
  Serial.print(draw_ms);
  Serial.println("ms");

  FastLED.delay(2000 - draw_ms);

  Serial.println("Showing green...");
  colorPattern(CRGB::Green);
  // TODO: fastled.delay is sending refreshes too quickly and crashing
  // FastLED.show();
  // delay(1500);
  FastLED.delay(2000);

  Serial.println("Showing blue...");
  colorPattern(CRGB::Blue);
  // TODO: fastled.delay is sending refreshes too quickly and crashing
  // FastLED.show();
  // delay(1500);
  FastLED.delay(2000);
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

  // setup array for sorting
  for (uint i = 0; i < numFreqBands; i++) {
    sortedLevelIndex[i] = i;
  }
}

void setup() {
  debug_serial(115200, 2000);

  Serial.println("Setting up...");

  // setup SPI for the Audio board (which has an SD card reader)
  SPI.setMOSI(SPI_MOSI_PIN);
  SPI.setSCK(SPI_SCK_PIN);

  SPI.begin(); // should this be here?

  setupSD();

  // right now, once we setup the lights, we can't use the SD card anymore
  setupLights();

  setupAudio();

  setupFFTBins();

  Serial.println("Starting...");
}

// we could/should pass fft and level as args
float updateLevelsFromFFT() {
  // https://forum.pjrc.com/threads/32677-Is-there-a-logarithmic-function-for-FFT-bin-selection-for-any-given-of-bands

  // read the FFT frequencies into numOutputs levels
  // music is heard in octaves, but the FFT data
  // is linear, so for the higher octaves, read
  // many FFT bins together.

  float overall_max = 0;

  for (uint i = 0; i < numFreqBands - 1; i++) {
    currentLevel[i] = fft1024.read(freqBands[i], freqBands[i + 1] - 1);

    if (currentLevel[i] > overall_max) {
      overall_max = currentLevel[i];
    }
  }

  // the last level always goes to maxBin
  currentLevel[numFreqBands - 1] = fft1024.read(freqBands[numFreqBands - 1], maxBin);

  return overall_max;
}

float getLocalMaxLevel(uint i, float scale_neighbor, float overall_max, float scale_overall_max) {
  float localMaxLevel = maxLevel[i];

  // check previous level if we aren't the first level
  if (i != 0) {
    localMaxLevel = max(localMaxLevel, maxLevel[i - 1] * scale_neighbor);
  }

  // check the next level if we aren't the last level
  if (i != numFreqBands) {
    localMaxLevel = max(localMaxLevel, maxLevel[i + 1] * scale_neighbor);
  }
  
  // check all the other bins, too
  if (overall_max and scale_overall_max) {
    localMaxLevel = max(localMaxLevel, overall_max * scale_overall_max);
  }

  return localMaxLevel;
}

void updateFrequencyColors() {
  // read FFT frequency data into a bunch of levels. assign each level a color and a brightness
  float overall_max = updateLevelsFromFFT();

  float local_max = 0;

  // turn off any quiet levels. we do this before turning any lights on so that our loudest frequencies are most
  // responsive
  for (uint i = 0; i < numFreqBands; i++) {
    // update maxLevel
    // TODO: don't just track max. track the % change. then do something with stddev of neighbors?
    if (currentLevel[i] > maxLevel[i]) {
      maxLevel[i] = currentLevel[i];
    }

    if (! frequencyColors[i].value) {
      // this light is already off
      continue;
    }

    local_max = getLocalMaxLevel(i, scale_neighbor_max, overall_max, scale_overall_max);

    // turn off if current level is less than the activation threshold
    // TODO: i'm not sure i like this method anymore. now that we have a y-axis, we can show the true level (it bounces around wild though so will need smoothing)
    if (currentLevel[i] < local_max * activate_difference) {
      // the output should be off

      // reduce the brightness at 2x the rate we reduce max level
      // we were using "video" scaling to fade (meaning: never fading to full black), but CHSV doesn't have a fadeLightBy method
      // frequencyColors[i].fadeLightBy(int((1.0 - decayMax) * 4.0 * 255));
      // TODO: should the brightness be tied to the currentLevel somehow? that might make it too random looking but now that we have better height calculations, maybe it should
      frequencyColors[i].value *= decayMax;

      if (millis() < turnOffMsArray[i]) {
        // the output has not been on for long enough to prevent flicker
        // make sure we don't turn off while fading
        // TODO: tune this minimum?
        if (frequencyColors[i].value < 1) {
          frequencyColors[i].value = 1;
        }
      } else {
        // the output has been on for at least minOnMs and is quiet now
        // if it is on, dim it quickly to off
        frequencyColors[i].value *= decayMax;
        if (frequencyColors[i].value > fade_factor) {
          frequencyColors[i].value -= fade_factor;
        } else {
          frequencyColors[i].value = 0;
          numOn -= 1;
        }
      }
    }
  }

  // sort the levels normalized against their max
  // this allows us to prioritize turning on for the loudest sounds
  qsort(sortedLevelIndex, numFreqBands, sizeof(float), compare_levels);

  // turn on up to maxOn loud levels in order of loudest to quietest
  for (uint j = 0; j < numFreqBands; j++) {
    uint i = sortedLevelIndex[j];

    local_max = getLocalMaxLevel(i, scale_neighbor_max, overall_max, scale_overall_max);

    // check if current is close to the last max (also check the neighbor maxLevels)
    if (currentLevel[i] >= local_max * activate_difference) {
      // this light should be on!
      if (numOn >= maxOn) {
        // except we already have too many lights on! don't do anything since this light is already off
        // don't break the loop because we still want to decay max level and process other lights
      } else {
        // we have room for the light! turn it on

        // if it isn't already on, increment numOn
        if (!frequencyColors[i].value) {
          // track to make sure we don't turn too many lights on. some configurations max out at 6.
          // we don't do this every time because it could have already been on, but now we made it brighter
          numOn += 1;
        }

        // TODO: color-blind color pallete
        // map(value, fromLow, fromHigh, toLow, toHigh)
        uint color_hue = map(i, 0, numFreqBands, 0, 255);
        // use 255 as the max brightness. if that is too bright, FastLED.setBrightness can be changed in setup to reduce
        // what 255 does

        // look at neighbors and use their max for brightness if they are louder (but don't be less than 10% on!)
        // TODO: s-curve? i think FastLED actually does a curve for us
        // TODO: what should the min be? should we limit how fast it moves around by including frequencyColors[i].value here?
        // notie how we getLocalMax but exclude the overall volume. we only want that for on/off. if we include it here everything flickers too much
        uint color_value = constrain(uint(currentLevel[i] / getLocalMaxLevel(i, scale_neighbor_max, 0, 0) * 255), value_min, uint(255));

        // https://github.com/FastLED/FastLED/wiki/FastLED-HSV-Colors#color-map-rainbow-vs-spectrum
        // HSV makes it easy to cycle through the rainbow
        // TODO: color from a pallet instead.
        // TODO: what saturation?
        frequencyColors[i] = CHSV(color_hue, 255, color_value);

        // make sure we stay on for a minimum amount of time
        // if we were already on, extend the time that we stay on
        turnOffMsArray[i] = millis() + minOnMs;
      }
    }

    // decay maxLevel
    // TODO: tune this. not sure a simple % decay is a good idea. might be better to subtract a fixed amount instead
    maxLevel[i] = maxLevel[i] * decayMax;

    // don't let the max ever go to zero otherwise so that it turns off when its quiet instead of activating at a whisper
    if (maxLevel[i] < minMaxLevel) {
      maxLevel[i] = minMaxLevel;
    }
  }

  // debug print
  // TODO: wrap this in an ifdef DEBUG
  for (uint i = 0; i < numFreqBands; i++) {
    Serial.print("| ");

    // TODO: maybe do something with parity here? i think i don't have enough lights for that to matter at this point.
    // do some research

    if (frequencyColors[i].value) {
      // Serial.print(leds[i].getLuma() / 255.0);
      // Serial.print(currentLevel[i]);
      Serial.print(frequencyColors[i].value / 255.0, 2);
    } else {
      Serial.print("    ");
    }
  }
  Serial.print("| ");
  Serial.print(AudioMemoryUsageMax());
  Serial.print(" blocks | Num On=");
  Serial.print(numOn);
  Serial.print(" | ");

  // finish debug print
  Serial.print(millis() - lastUpdate);
  Serial.println("ms");
  lastUpdate = millis();
  Serial.flush();
}

void mapFrequencyColorsToOutputs() {
  for (uint i = 0; i < numOutputs; i++) {
    // numFreqBands can be bigger or smaller than numOutputs. a simple map like this works fine if numOutputs >
    // numFreqBands, but if not it skips some
    if (numOutputs == numFreqBands) {
      outputs[i] = frequencyColors[i];
    } else if (numOutputs > numFreqBands) {
      // spread the frequency bands out; multiple LEDs for one frequency
      outputs[i] = frequencyColors[map(i, 0, numOutputs, 0, numFreqBands)];
    } else {
      // shrink frequency bands down. pick the brightest color
      // TODO: I don't think this is working

      // start by setting it to the first available band.
      uint bottomFreqId = map(i, 0, numOutputs, 0, numFreqBands);

      outputs[i] = frequencyColors[bottomFreqId];

      uint topFreqId = map(i + 1, 0, numOutputs, 0, numFreqBands);
      for (uint f = bottomFreqId + 1; f < topFreqId; f++) {
        if (!frequencyColors[f].value) {
          // TODO: dim it some to represent neighbor being off?
          continue;
        }

        if (!outputs[i].value) {
          // output is off, simply set the color as is
          outputs[i] = frequencyColors[f];
        } else {
          // output has multiple frequencies to show
          // TODO: don't just replace with the brighter. instead increase the brightness and shift the color or
          // something to combine outputs[i] and frequencyColors[f]
          if (outputs[i].value < frequencyColors[f].value) {
            outputs[i] = frequencyColors[f];
          }
        }
      }
    }
  }
}

// TODO: args instead of globals
void mapOutputsToSpreadOutputs() {
  // TODO: this seems really inefficient since a ton of spots will just be black. it makes the code simple though
  for (uint i = 0; i < numSpreadOutputs; i += ledsPerSpreadOutput) {
    outputsStretched[i] = outputs[i / ledsPerSpreadOutput];
  }
}

float map_float(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// TODO: args instead of globals
void mapSpreadOutputsToVisualizerMatrix() {
  // shift increments each frame and is used to slowly modify the pattern
  // TODO: test this now that we are on a matrix
  // TODO: i don't like this shift method. it should fade the top pixel and work its way down, not dim the whole column evenly
  static uint shift = 0;

  // TODO: should this be static?
  static CHSV new_color;

  for (uint x = 0; x < visualizerNumLEDsX; x++) {
    uint shifted_x = (shift / frames_per_shift + x) % visualizerNumLEDsX;

    if (numSpreadOutputs == visualizerNumLEDsX) {
      new_color = outputsStretched[shifted_x];
    } else {
      // numFreqBands can be bigger or smaller than numOutputs
      // TODO: test this with large and small values of numSpreadOutputs vs numLEDs
      if (numSpreadOutputs < visualizerNumLEDsX) {
        // simple repeat of the pattern
        new_color = outputsStretched[shifted_x % numSpreadOutputs];
      } else {
        // pattern is larger than numLEDs
        new_color = outputsStretched[shifted_x % visualizerNumLEDsX];
      }
    }

    if (new_color.value >= value_min) {
      // use the value to calculate the height for this color
      // TODO: tune this. we might want a more interesting curve
      // if value == 255, highestIndexToLight will be 8. This means the whole column will be max brightness
      // notice that we do NOT use value_min for in_min on map. instead we use the actual range of the LED
      float highestIndexToLight_f = map_float(new_color.value, 0, 255, 0, visualizerNumLEDsY);

      uint highestIndexToLight = uint(highestIndexToLight_f);

      // we are using height instead of brightness to represent how loud the frequency was
      // so set to max brightness
      new_color.value = 255;

      for (uint y = 0; y < visualizerNumLEDsY; y++) {
        if (y < highestIndexToLight) {
          visualizer_matrix(x, y) = new_color;
        } else if (y == highestIndexToLight) {
          // the highest lit pixel will have a variable brightness to match the volume
          // TODO: tune this. we might want a more interesting curve. value_min night need to be a larger value to prevent flickering
          new_color.value = uint(map_float(highestIndexToLight_f - y, 0.0, 1.0, value_min, 255.0));

          visualizer_matrix(x, y) = new_color;
        } else {
          // TODO: not sure if this should fade or go direct to black. we already have fading on the visualizer
          // visualizer_matrix(x, y).fadeToBlackBy(fade_factor * 2);
          visualizer_matrix(x, y) = CRGB::Black;
        }
      }
    } else {
      // if new_color is black or close to it, we fade rather then set to black
      // TODO: this doesn't look good. fade the top led until it is off, and then move on to the next instead of fading all equally
      for (uint y = 0; y < numLEDsY; y++) {
        // visualizer_matrix(x, y).fadeToBlackBy(fade_factor);
        visualizer_matrix(x, y) = CRGB::Black;
      }
    }
  }

  shift++;
}

void combineMatrixes() {
  // TODO: what should we do here? how should we overlay/interleave the different matrixes into one?

  // TODO: this could probably be a lot more efficient
  for (uint16_t x = 0; x < numLEDsX; x++) {
    for (uint16_t y = 0; y < numLEDsY; y++) {
      // TODO: if text, display text
      // TODO: else if sprite, display sprite
      // TODO: else display visualizer (wrapping on the x axis)
      // TODO: do more interesting things with y. maybe set the middle of the array as the "bottom" and grow out
      if (y < visualizerNumLEDsY) {
        uint16_t vis_x = x % visualizerNumLEDsX;
        uint16_t vis_y = y % visualizerNumLEDsY;
        leds(x, y) = visualizer_matrix(vis_x, vis_y);
      } else {
        leds(x, y) = CRGB::Black;
      }
    }
  }
}

bool new_frame = false;
bool new_sprite_frame = false;

unsigned long loop_duration = 0;

void loop() {
  loop_duration = millis();

  if (fft1024.available()) {
    updateFrequencyColors();

    // TODO: pass args to these functions instead of modifying globals
    mapFrequencyColorsToOutputs();
    mapOutputsToSpreadOutputs();
    mapSpreadOutputsToVisualizerMatrix();

    new_frame = true;
  }

  // TODO: draw text/sprites and set new_frame=true

  if (new_frame) {
    // TODO: time this
    combineMatrixes();

    new_frame = false;

    loop_duration = millis() - loop_duration;
    if (loop_duration < ms_per_frame) {
      // using FastLED's delay allows for dithering
      // TODO: calculate the delay to get an even framerate now that show takes an uneven amount of time. (13-15ms)
      // TODO: with software spi, fastled.delay was sending refreshes too quickly and crashing. try with hardware
      // delay calls FastLED.show multiple times. since we had to reduce bandwidth, this takes time that we subtract from our delay
      long delay = ms_per_frame - loop_duration - draw_ms;
      FastLED.delay(delay);

      // TODO: use a regular delay here to keep the framerate slower?
    } else {
      Serial.print("Running slow! ");
      Serial.println(loop_duration);
      FastLED.show();
    }
  }
}
