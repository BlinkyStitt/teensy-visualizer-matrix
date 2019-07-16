#define DEBUG
// #define DEBUG_SERIAL_WAIT
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

#include "config.h"

uint16_t freqBands[numFreqBands];

// keep track of the current levels. this is a sum of multiple frequency bins.
// TODO: keep track of the average magnitude
// keep track of the max volume for each frequency band (slowly decays)
struct frequency {
  float current_magnitude;
  // float average_magnitude;
  float max_magnitude;
};
frequency frequencies[numFreqBands] = {0, 0};

CHSV frequencyColors[numFreqBands];

// frequencyColors are stretched/squished to fit this (squishing being what you probably want)
// TODO: rename this to something more descriptive. or maybe get rid of it and store directly on the visualizer_matrix
CHSV outputs[numOutputs];

// outputs are stretched to fit this
// TODO: rename this to something more descriptive. or maybe get rid of it and store directly on the visualizer_matrix
CHSV outputsStretched[numSpreadOutputs];

// TODO: not sure if HORIZONTAL_ZIGZAG_MATRIX is actually what we want. we will test when the LEDs arrive
// TODO: we might want negative for Y, but using uint16_t is breaking that
cLEDMatrix<visualizerNumLEDsX, visualizerNumLEDsY, VERTICAL_ZIGZAG_MATRIX> visualizer_matrix;

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

// keep track of when to turn lights off so they don't flicker
unsigned long turnOnMsArray[numFreqBands];
unsigned long turnOffMsArray[numFreqBands];

// used to keep track of framerate // TODO: remove this if debug mode is disabled
unsigned long draw_ms = 8;
unsigned long lastUpdate = 0;
unsigned long lastDraw = 0;

float FindE(uint16_t bands, uint16_t minBin, uint16_t maxBin) {
  // https://forum.pjrc.com/threads/32677-Is-there-a-logarithmic-function-for-FFT-bin-selection-for-any-given-of-bands?p=133842&viewfull=1#post133842
  float increment = 0.1, eTest, n;
  uint16_t b, count, d;

  for (eTest = 1; eTest < maxBin; eTest += increment) { // Find E through brute force calculations
    count = minBin;
    for (b = 0; b < bands; b++) { // Calculate full log values
      n = pow(eTest, b);
      d = n + 0.5;  // round up
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

  // TODO: time this function. i might want to call it while the program is running to tune to whatever was recently heard
  float e, n;
  uint16_t count = minBin, d;

  e = FindE(numFreqBands, minBin, maxBin); // Find calculated E value

  // TODO: find multiple values for different minBin and maxBins. Then if we detect that there is no bass or no highs, we can spread our limited colors over just the activate frequences

  if (e) {                           // If a value was returned continue
    Serial.printf("E = %4.4f\n", e); // Print calculated E value
    Serial.printf("  i  low high\n");
    for (uint16_t b = 0; b < numFreqBands; b++) { // Test and print the bins from the calculated E
      n = pow(e, b);
      d = n + 0.5;

      Serial.printf("%3d ", b);

      Serial.printf("%4d ", count); // Print low bin
      freqBands[b] = count;  // Save the low bin to a global

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
  for (uint8_t x = 0; x < numLEDsX; x++) {
    uint8_t y = x % numLEDsY;
    leds(x, y) = color;
  }
}

void setupLights() {
  // TODO: turn off onboard LED
  // TODO: clock select pin for FastLED to OUTPUT like we do for the SDCARD?

  // with software spi, for one panel, 2mhz worked. when a second panel was added, 2mhz crashed after a few seconds, but 1mhz is working on my test code. crashed after a second or so though 
  // looks like 500 mhz can run 2 panels, but we are having power troubles now. more power might mean we can increase the rate
  // with 5.0v over usb, i can only run at 1500kHz
  // TODO: it was working well at 2000kHz until the battery ran down, then i lowered the rate and it worked. when i put a new battery in, it crashed though
#ifdef LED_DATA_RATE_KHZ
  Serial.println("Setting up dotstars...");
  FastLED.addLeds<LED_CHIPSET, MATRIX_DATA_PIN, MATRIX_CLOCK_PIN, LED_MODE, DATA_RATE_KHZ(LED_DATA_RATE_KHZ)>(leds[0], leds.Size()).setCorrection(TypicalSMD5050);
#else
  Serial.println("Setting up neopixels...");
  FastLED.addLeds<LED_CHIPSET, MATRIX_DATA_PIN>(leds[0], leds.Size()).setCorrection(TypicalSMD5050);
#endif

  // TODO: what should this be set to? the flexible panels are much larger
  // led matrix max is 15 amps, but because its flexible, best to keep it max of 5 amps. then we have 2 boards, so multiply by 2
  FastLED.setMaxPowerInVoltsAndMilliamps(3.7, 5 * 1000);
  // FastLED.setMaxPowerInVoltsAndMilliamps(5.0, 120); // when running through teensy's usb port, the max draw is much lower than with a battery

  FastLED.setBrightness(DEFAULT_BRIGHTNESS); // TODO: read this from the SD card. or allow tuning with the volume knob

  // clear all the arrays
  // TODO: init them empty instead
  for (uint8_t i = 0; i < numOutputs; i++) {
    outputs[i].value = 0;
  }
  for (uint8_t i = 0; i < numSpreadOutputs; i++) {
    outputsStretched[i].value = 0;
  }

  FastLED.clear(true);

  // show red, green, blue, so that we make sure LED_MODE is correct

  Serial.println("Showing red...");
  colorPattern(CRGB::Red);

  // time FastLED.show so we can delay the right amount in our main loop
  draw_ms = millis();
  FastLED.show();
  draw_ms = millis() - draw_ms;

  Serial.print("Draw time for show: ");
  Serial.print(draw_ms);
  Serial.println("ms");

  // now delay for more time to make sure that fastled can power this many lights and update with this bandwidth
  FastLED.delay(1500 - draw_ms);

  Serial.println("Showing green...");
  colorPattern(CRGB::Green);
  FastLED.delay(1500);

  Serial.println("Showing blue...");
  colorPattern(CRGB::Blue);
  FastLED.delay(1500);
}

void setupAudio() {
  // Audio requires memory to work. I haven't seen this go over 11
  AudioMemory(12);

  // Enable the audio shield and set the output volume.
  audioShield.enable();
  audioShield.muteHeadphone(); // to avoid any clicks
  audioShield.inputSelect(AUDIO_INPUT_MIC);
  audioShield.volume(0.5);
  audioShield.micGain(63); // was 63, then 40  // 0-63 // TODO: tune this

  // audioShield.audioPreProcessorEnable(); // todo: pre or post?

  // bass, mid_bass, midrange, mid_treble, treble
  // TODO: tune this. maybe read from SD card
  // audioShield.eqSelect(GRAPHIC_EQUALIZER);
  // audioShield.eqBands(-0.80, -0.75, -0.50, 0.50, 0.80);  // the great northern
  // audioShield.eqBands(-0.5, -.2, 0, .2, .5);  // todo: tune this
  // audioShield.eqBands(-0.80, -0.10, 0, 0.10, 0.33);  // todo: tune this
  //audioShield.eqBands(0.0, 0.0, 0.0, 0.1, 0.33); // todo: tune this
  // audioShield.eqBands(0.5, 0.5, 0.0, 0.0, 0.0); // todo: tune this

  audioShield.unmuteHeadphone(); // for debugging
}

void setupRandom() {
  randomSeed(analogRead(3));

#ifdef DEBUG
  Serial.println(random(100));
  Serial.println(random(100));
  Serial.println(random(100));
  Serial.println(random(100));
#endif
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

  setupRandom();

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

  for (uint16_t i = 0; i < numFreqBands; i++) {
    if (i < numFreqBands - 1) {
      frequencies[i].current_magnitude = fft1024.read(freqBands[i], freqBands[i + 1] - 1);
    } else {
      // the last level always goes to maxBin
      frequencies[i].current_magnitude = fft1024.read(freqBands[numFreqBands - 1], maxBin);
    }

    if (frequencies[i].current_magnitude > frequencies[i].max_magnitude) {
      frequencies[i].max_magnitude = frequencies[i].current_magnitude;
    } else {
      // frequencies[i].max_magnitude *= decayMax;
    }

    // don't let the max ever go to zero otherwise so that it turns off when its quiet instead of activating at a whisper
    if (frequencies[i].max_magnitude < minMaxLevel) {
      frequencies[i].max_magnitude = minMaxLevel;
    }

    if (frequencies[i].max_magnitude > overall_max) {
      overall_max = frequencies[i].max_magnitude;
    }
  }

  return overall_max;
}

float getLocalMaxLevel(uint16_t i, float scale_neighbor, float overall_max, float scale_overall_max) {
  float localMaxLevel = frequencies[i].max_magnitude;

  // check previous level if we aren't the first level
  if (i != 0) {
    localMaxLevel = max(localMaxLevel, frequencies[i - 1].max_magnitude * scale_neighbor);
  }

  // check the next level if we aren't the last level
  if (i != numFreqBands) {
    localMaxLevel = max(localMaxLevel, frequencies[i + 1].max_magnitude * scale_neighbor);
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

  for (uint16_t i = 0; i < numFreqBands; i++) {
    // check if current magnitude is close to the max magnitude
    // TODO: i thought i wanted "if (millis() >= turnOffMsArray[i] && frequencies[i].current_magnitude / local_max >= activate_difference) {"
    if (frequencies[i].current_magnitude >= overall_max * activate_difference) {
      if (millis() < turnOnMsArray[i]) {
        // nevermind! we need to wait longer to reduce flicker
        continue;
      }

      // https://github.com/FastLED/FastLED/wiki/FastLED-HSV-Colors#color-map-rainbow-vs-spectrum
      // HSV makes it easy to cycle through the rainbow
      // TODO: color-blind color pallete
      // map(value, fromLow, fromHigh, toLow, toHigh)
      // TODO: fastLED has a map8 function or something like that that i think is faster and meant for this
      uint8_t color_hue = map(i, 0, numFreqBands, 0, 255);

      // use 255 as the max brightness. if that is too bright, FastLED.setBrightness can be changed in setup to reduce
      // what 255 does
      // uint8_t color_value = 
      // TODO: -= fade_rate instead?
      // use max to let it climb quickly but fall slowly
      // TODO: we were doing  * decayMax here, but we do it elsewhere
      // TODO: i think we should do an exponential moving average on this
      // uint8_t color_value = max(color_value, frequencyColors[i].value);

      uint8_t reading = frequencies[i].current_magnitude / overall_max * 255;

      // exponential moving average
      float alpha = 0.98;
      float lastOutput = frequencyColors[i].value;
      float alphaScale = 1.0;
      uint8_t ema = (alpha * reading + (alphaScale - alpha) * lastOutput) / alphaScale;

      // TODO: make sure this doesn't wrap
      uint8_t lastOutputDecreased = frequencyColors[i].value * decayMax - fade_rate;

      uint8_t color_value;
      if (ema > lastOutputDecreased) {
        // if the magnitude is increasing, or only a little less than the current value set it to the ema
        color_value = ema;
      } else {
        // if the magnitude is decreasing, decrease at a fixed rate
        color_value = lastOutputDecreased;
      }

      // hue matches the frequency
      // max saturation
      // value matches the magnitude for the frequency
      frequencyColors[i] = CHSV(color_hue, 255, color_value);

      // make sure we stay on for a minimum amount of time. this prevents flickering if the magnitude changes quickly
      turnOnMsArray[i] = millis() + minOnMs / 2.1;
      turnOffMsArray[i] = millis() + minOnMs;
    } else {  // if (frequencies[i].current_magnitude < overall_max * activate_difference)
      // the current magnitude is not close to the max magnitude. turn it down if we have waited long enough

      if (millis() < turnOffMsArray[i]) { 
        // nevermind! we need to wait longer to reduce flicker
        continue;
      }

      // TODO: i dont think this does exactly what we want here. we divide value by max. go back to using fade_factor?
      frequencies[i].max_magnitude *= decayMax;

      if (frequencyColors[i].value == 0) {
        // this light is already off
        continue;
      }
 
      // the output has been on for at least minOnMs and is quiet now. turn it down

      // reduce the brightness at 2x the rate we reduce max level
      // we were using "video" scaling to fade (meaning: never fading to full black), but CHSV doesn't have a fadeLightBy method
      // frequencyColors[i].fadeLightBy(int((1.0 - decayMax) * 4.0 * 255));
      // TODO: should the brightness be tied to the currentLevel somehow? that might make it too random looking but now that we have better height calculations, maybe it should
      frequencyColors[i].value *= decayMax;
      frequencyColors[i].value *= decayMax;
    }
  }

  // debug print
#ifdef DEBUG
  for (uint16_t i = 0; i < numFreqBands; i++) {
    Serial.print("| ");

    // TODO: maybe do something with parity here? i think i don't have enough lights for that to matter at this point.
    // do some research

    if (frequencyColors[i].value) {
      // Serial.print(leds[i].getLuma() / 255.0);
      // Serial.print(currentLevel[i]);
      Serial.print(frequencyColors[i].value / 255.0, 2);
      // Serial.print(frequencies[i].current_magnitude);
    } else {
      Serial.print("    ");
    }
  }
  Serial.print("| ");
  Serial.print(AudioMemoryUsageMax());
  Serial.print(" blocks | ");

  // finish debug print
  Serial.print(millis() - lastUpdate);
  Serial.println("ms");
  lastUpdate = millis();
  Serial.flush();
#endif
}

void mapFrequencyColorsToOutputs() {
  for (uint16_t i = 0; i < numOutputs; i++) {
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
      uint16_t bottomFreqId = map(i, 0, numOutputs, 0, numFreqBands);

      outputs[i] = frequencyColors[bottomFreqId];

      uint16_t topFreqId = map(i + 1, 0, numOutputs, 0, numFreqBands);
      for (uint16_t f = bottomFreqId + 1; f < topFreqId; f++) {
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
  // TODO: make it 2 wide bars instead of a gap. or maybe have a gap, too
  for (uint16_t i = 0; i < numSpreadOutputs; i += ledsPerSpreadOutput) {
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
  // TODO: the top pixels are flickering a lot, too. maybe we need minOnMs here instead of earlier?
  static uint16_t shift = 0;
  static uint16_t frames_since_last_shift = 0;

  // TODO: should this be static?
  static CHSV new_color;

  // cycle between different patterns
  static bool should_flip_y[visualizerNumLEDsX] = {false};
  static uint16_t map_visualizer_y[visualizerNumLEDsY] = {0};
  static bool flip_y = false;
  static bool new_pattern = true;
  static bool reverse_rotation = true;
  static uint8_t frames_per_shift_index = 0;
  static uint8_t current_frames_per_shift = frames_per_shift[frames_per_shift_index];
  // static unsigned long next_change_frames_per_shift = 0;

  static uint8_t lowestIndexToLight = 1;
  static uint8_t lowestIndexToLightWhite = 4;

  // OPTION 1: cycle frames_per_shift every X seconds
  // TODO: every X seconds change the frames_per_shift
  // if (millis() >= next_change_frames_per_shift) {
  //   frames_per_shift_index++;
  //   // TODO: we have 3, but slow is boring
  //   if (frames_per_shift_index >= 2) {
  //     frames_per_shift_index = 0;
  //   }
  //   // TODO: different lengths for different modes
  //   // TODO: have a struct for this
  //   next_change_frames_per_shift = millis() + 3000;
  // }
  // // TODO: do an interesting curve on current_frames_per_shift to head towards frames_per_shift[frames_per_shift_index]. ema might work for now
  // current_frames_per_shift = frames_per_shift[frames_per_shift_index];

  // OPTION 2: oscillate frames_per_shift between a slow and a fast speed
  // static uint16_t loud_frame_counter = 0;
  // bool increment_loud_frame_counter = false;
  // current_frames_per_shift = map(cubicwave8(loud_frame_counter), 0, 255, frames_per_shift[0], frames_per_shift[2]);
  // Serial.print("frames_per_shift: ");
  // Serial.println(current_frames_per_shift);

  // OPTION 3: if loud_frame_counter was incremented multiple times in one frame, have a chance to rotate once at high speed instead of changing direction

  // TODO: if we are going too fast for too long, slow down

  if (new_pattern) {
    for (uint8_t y = 0; y < visualizerNumLEDsY; y++) {
      map_visualizer_y[y] = 7 - y;
    }
    // TODO: this is too simplistic. i want a pattern where it goes outward and inward

    new_pattern = false;
  }

  for (uint8_t x = 0; x < visualizerNumLEDsX; x++) {
    // TODO: restructure this to change the rate of frames_per_shift.
    // we take the absolute value because shift might negative
    uint8_t shifted_x = abs((x + shift) % visualizerNumLEDsX);

    // draw a border
    uint8_t i = x % numSpreadOutputs / ledsPerSpreadOutput;
    // TODO: use fastLED function instead of map
    uint8_t color_hue = map(i, 0, numFreqBands, 0, 255);

    CHSV border_color = CHSV(color_hue, 255, value_visualizer);

    visualizer_matrix(shifted_x, 0) = border_color;
    visualizer_matrix(shifted_x, visualizerNumLEDsY - 1) = border_color;

    if (numSpreadOutputs == visualizerNumLEDsX) {
      new_color = outputsStretched[x];
    } else {
      // numFreqBands can be bigger or smaller than numOutputs
      // TODO: test this with large and small values of numSpreadOutputs vs numLEDs
      if (numSpreadOutputs < visualizerNumLEDsX) {
        // simple repeat of the pattern
        new_color = outputsStretched[x % numSpreadOutputs];
      } else {
        // pattern is larger than numLEDs
        new_color = outputsStretched[x % visualizerNumLEDsX];
      }
    }

    if (new_color.value > 0) {
      // use the value to calculate the height for this color
      // if value == 255, highestIndexToLight will be 8. This means the whole column will be max brightness
      // TODO: should we do the frequences[...].average_magnitude / local_max calculations here?
      uint8_t highestIndexToLight = map(new_color.value, 1, 255, lowestIndexToLight, visualizerNumLEDsY - 1);

      // we are using height instead of brightness to represent how loud the frequency was
      // so set to max brightness
      new_color.value = value_visualizer;

      for (uint8_t y = lowestIndexToLight; y <= visualizerNumLEDsY - 1; y++) {
        uint8_t shifted_y = y;
        if (should_flip_y[x]) {
          shifted_y = map_visualizer_y[shifted_y];
        }

        if (y < highestIndexToLight) {
          // simple color bar
          visualizer_matrix(shifted_x, shifted_y) = new_color;
        } else if (y == highestIndexToLight) {
          if (y < lowestIndexToLightWhite) {
            // very short bars shouldn't have any white at the top
            visualizer_matrix(shifted_x, shifted_y) = new_color;
          } else {
            // taller bars should have white at the top
            visualizer_matrix(shifted_x, shifted_y) = CRGB::White;
          }

          if (highestIndexToLight >= visualizerNumLEDsY - 1) {
            // loud_frame_counter++;
            // increment_loud_frame_counter = true;

            if (random(100) < 50) {
              EVERY_N_SECONDS(3) {
                // TODO: instead of a hard rotate, cycle speeds. 
                reverse_rotation = !reverse_rotation;
                frames_since_last_shift = current_frames_per_shift;
              }
            } else {
              EVERY_N_SECONDS(3) {
                // if we hit the top, light both ends white and flip this for the next time
                visualizer_matrix(shifted_x, 0) = CRGB::White;

                flip_y = should_flip_y[x] = !should_flip_y[x];
              }
            }
          }
        } else if (y < visualizerNumLEDsY - 1) {
          // fill the rest in black (except the border)
          // TODO: not sure if this should fade or go direct to black. we already have fading on the visualizer
          // visualizer_matrix(x, y).fadeToBlackBy(fade_factor * 2);
          visualizer_matrix(shifted_x, shifted_y) = CRGB::Black;
        }
      }
    } else {
      // it should be off

      for (uint8_t y = lowestIndexToLight; y < numLEDsY - 1; y++) {
        // visualizer_matrix(x, y).fadeToBlackBy(fade_factor);
        visualizer_matrix(shifted_x, y) = CRGB::Black;
      }

      // follow the loudest sound
      // TODO: do this in a seperate loop so that flip_y is the same for all entries?
      should_flip_y[x] = flip_y;
    }
  }

  // if (increment_loud_frame_counter) {
  //   loud_frame_counter++;
  // }

  frames_since_last_shift++;
  if (frames_since_last_shift >= current_frames_per_shift) {
    frames_since_last_shift = 0;

    // TODO: maybe stay still for a frame if reverse_rotation just changed?
    if (reverse_rotation) {
      shift--;
    } else {
      shift++;
    }
  }
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

  // TODO: EVERY_N_MILLIS(...) { draw text/sprites and set new_frame=true }

  if (new_frame) {
    new_frame = false;

    combineMatrixes();

    // the time to draw the audio/text/sprite is variable
    loop_duration = millis() - loop_duration;

    // Serial.print("loop duration: ");
    // Serial.println(loop_duration);

    // using FastLED's delay allows for dithering by calling FastLED.show multiple times
    // showing can take a noticable time (especially with a reduced bandwidth) that we subtract from our delay
    // so unless we can fit at least 2 draws in, just do regular show
    long draw_delay = ms_per_frame - loop_duration - draw_ms;
    if (draw_delay >= long(draw_ms * 2)) {
      // Serial.print("Delaying for ");
      // Serial.print(draw_delay);
      // Serial.print(" + ");
      // Serial.println(draw_ms);

      unsigned long actual_delay = millis();

      FastLED.delay(draw_delay);

      actual_delay = millis() - actual_delay;

      // Serial.print("actual delay: ");
      // Serial.println(actual_delay);

      // TODO: does delaying like this to keep an even framerate make sense?
      if (actual_delay < draw_delay + draw_ms) {
        // Serial.println("framerate fix delay...");
        delay(draw_delay + draw_ms - actual_delay);
      }
    } else {
      // Serial.print("Running too slow for delay! ");
      // Serial.println(draw_delay);

      FastLED.show();
    }
  }
}
