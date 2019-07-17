#define DEBUG
// #define DEBUG_SERIAL_WAIT
#include "bs_debug.h"

#include <stdlib.h>

#include <Audio.h>
#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDSprites.h>
#include <LEDText.h>
#include <ResponsiveAnalogRead.h>
#include <SD.h>
#include <SPI.h>
#include <SerialFlash.h>
#include <Wire.h>

#include "config.h"

uint16_t freqBands[numFreqBands];

// keep track of the current levels. this is a sum of multiple frequency bins.
// keep track of the max volume for each frequency band (slowly decays)
struct frequency {
  float current_magnitude;
  float max_magnitude;
  uint8_t average_scaled_magnitude; // exponential moving average of current_magnitude divided by max_magnitude
  unsigned long turnOnMs;  // keep track of when we turned a light on so they don't flicker when we change them
  unsigned long turnOffMs;  // keep track of when we turned a light on so they don't flicker when we turn them off
};
frequency frequencies[numFreqBands] = {0, 0, 0, 0, 0};

// TODO: move this to a seperate file so that we can support multiple led/el light combinations
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

// make a ResponsiveAnalogRead object, pass in the pin, and either true or false depending on if you want sleep enabled
// enabling sleep will cause values to take less time to stop changing and potentially stop changing more abruptly,
// where as disabling sleep will cause values to ease into their correct position smoothly and with slightly greater accuracy
ResponsiveAnalogRead volume_knob(VOLUME_KNOB, false);

// used to keep track of framerate // TODO: remove this if debug mode is disabled
unsigned long draw_ms = 4;
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
  // TODO: sin wave
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
  // the on/off switch only does 2 amps
  FastLED.setMaxPowerInVoltsAndMilliamps(3.7, 2000);
  // FastLED.setMaxPowerInVoltsAndMilliamps(5.0, 120); // when running through teensy's usb port, the max draw is much lower than with a battery

  setVisualizerBrightness();

  // clear all the arrays
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

void updateFrequencies() {
  // read FFT frequency data into a bunch of levels. assign each level a color and a brightness
  float overall_max = updateLevelsFromFFT();

  // exponential moving average
  float alpha = 0.98;
  float alphaScale = 1.0;

  for (uint16_t i = 0; i < numFreqBands; i++) {
    // check if current magnitude is close to the max magnitude

    // TODO: instead of scaling to an overall max. scale to some average of the overall and this frequency's max

    if (frequencies[i].current_magnitude >= overall_max * activate_difference) {
      if (millis() < frequencies[i].turnOnMs) {
        // nevermind! we need to wait longer before changing this in order to reduce flicker
        continue;
      }

      float scaled_reading = frequencies[i].current_magnitude / overall_max * 255;
      float lastOutput = frequencies[i].average_scaled_magnitude;

      uint8_t ema = (alpha * scaled_reading + (alphaScale - alpha) * lastOutput) / alphaScale;

      uint8_t lastOutputDecreased = frequencies[i].average_scaled_magnitude * decayMax;
      if (lastOutputDecreased < fade_rate) {
        lastOutputDecreased = 0;
      } else {
        lastOutputDecreased -= fade_rate;
      }

      if (ema > lastOutputDecreased) {
        // if the magnitude is increasing, or only a little less than the current value set it to the ema
        frequencies[i].average_scaled_magnitude = ema;
      } else {
        // if the magnitude is decreasing, decrease at a fixed rate
        frequencies[i].average_scaled_magnitude = lastOutputDecreased;
      }

      // make sure we stay on for a minimum amount of time. this prevents flickering if the magnitude changes quickly
      frequencies[i].turnOnMs = millis() + minOnMs / 2.1;
      frequencies[i].turnOffMs = millis() + minOnMs;
    } else {  // if (frequencies[i].current_magnitude < overall_max * activate_difference)
      // the current magnitude is not close to the max magnitude. turn it down if we have waited long enough

      if (millis() < frequencies[i].turnOffMs) { 
        // nevermind! we need to wait longer to reduce flicker
        continue;
      }

      // TODO: i dont think this does exactly what we want here. we divide value by max. go back to using fade_factor?
      frequencies[i].max_magnitude *= decayMax;

      if (frequencies[i].average_scaled_magnitude == 0) {
        // this light is already off
        continue;
      }
 
      // the output has been on for at least minOnMs and is quiet now. turn it down

      // reduce the brightness at 2x the rate we reduce max level
      // TODO: maybe change this to be more directly tied to time to reach 0
      frequencies[i].average_scaled_magnitude *= decayMax;
      frequencies[i].average_scaled_magnitude *= decayMax;
    }
  }

  // debug print
#ifdef DEBUG
  for (uint16_t i = 0; i < numFreqBands; i++) {
    Serial.print("| ");

    // TODO: maybe do something with parity here? 
    // do some research

    if (frequencies[i].average_scaled_magnitude > 0) {
      // Serial.print(leds[i].getLuma() / 255.0);
      Serial.print(frequencies[i].average_scaled_magnitude / 255.0, 2);
    } else {
      Serial.print("    ");
    }
  }
  Serial.print("| ");
  Serial.print(AudioMemoryUsageMax());
  Serial.print(" blocks | ");

  Serial.print(volume_knob.getValue());
  Serial.print(" vol | ");

  // finish debug print
  Serial.print(millis() - lastUpdate);
  Serial.println("ms");
  lastUpdate = millis();
  Serial.flush();
#endif
}

// TODO: args instead of globals
void mapFrequenciesToVisualizerMatrix() {
  // shift increments each frame and is used to slowly modify the pattern
  // TODO: test this now that we are on a matrix
  // TODO: i don't like this shift method. it should fade the top pixel and work its way down, not dim the whole column evenly
  // TODO: the top pixels are flickering a lot, too. maybe we need minOnMs here instead of earlier?
  static uint16_t shift = 0;
  static uint16_t frames_since_last_shift = 0;

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

  bool reversed_this_frame = false;

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

  // TODO: make this brighter (but don't overflow!)
  CHSV visualizer_white = CHSV(0, 0, visualizer_white_value);
  // visualizer_white.value *= 2;

  for (uint8_t x = 0; x < visualizerNumLEDsX; x++) {
    // we take the absolute value because shift might negative
    uint8_t shifted_x = abs((x + shift) % visualizerNumLEDsX);

    // TODO: color palettes instead of simple rainbow hue
    uint8_t visualizer_hue = map(x, 0, visualizerNumLEDsX - 1, 0, 255);

    CHSV visualizer_color = CHSV(visualizer_hue, 255, visualizer_color_value);

    uint8_t i = visualizerXtoFrequencyId[x];

    // draw a border
    visualizer_matrix(shifted_x, 0) = visualizer_color;
    visualizer_matrix(shifted_x, visualizerNumLEDsY - 1) = visualizer_color;

    if (i < numFreqBands && frequencies[i].average_scaled_magnitude > 0) {
      // use the value to calculate the height for this color
      // if value == 255, highestIndexToLight will be 7. This means the whole column will be max brightness
      uint8_t highestIndexToLight = map(frequencies[i].average_scaled_magnitude, 1, 255, lowestIndexToLight, visualizerNumLEDsY - 1);

      for (uint8_t y = lowestIndexToLight; y <= visualizerNumLEDsY - 1; y++) {
        uint8_t shifted_y = y;
        if (should_flip_y[x]) {
          shifted_y = map_visualizer_y[shifted_y];
        }

        if (y < highestIndexToLight) {
          // simple color bar
          visualizer_matrix(shifted_x, shifted_y) = visualizer_color;
        } else if (y == highestIndexToLight) {
          if (y < lowestIndexToLightWhite) {
            // very short bars shouldn't have any white at the top
            visualizer_matrix(shifted_x, shifted_y) = visualizer_color;
          } else {
            // taller bars should have white at the top
            visualizer_matrix(shifted_x, shifted_y) = visualizer_white;
          }

          if (highestIndexToLight >= visualizerNumLEDsY - 1) {
            // loud_frame_counter++;
            // increment_loud_frame_counter = true;

            uint8_t r = random(100);

            // TODO: this doesn't work as well with the bars being two wide. need configurable 
            if (r < 34) {
              EVERY_N_SECONDS(3) {
                // TODO: instead of a hard rotate, cycle speeds
                reversed_this_frame = true;
                reverse_rotation = !reverse_rotation; // TODO: enum instead of bool?
                frames_since_last_shift = current_frames_per_shift + 99;
              }
            } else if (r < 67) {
              EVERY_N_SECONDS(3) {
                // if we hit the top, light both ends white and flip this for the next time
                visualizer_matrix(shifted_x, 0) = visualizer_white;

                flip_y = should_flip_y[x] = !should_flip_y[x];
              }
            } else {
              // do nothing
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
      // the visualizer (but not the border!) should be off
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
    if (!reversed_this_frame) {
      if (reverse_rotation) {
        shift--;
      } else {
        shift++;
      }
    }
  }
}

void setVisualizerBrightness() {
  static int last_brightness = 0;

  volume_knob.update();

  if (volume_knob.hasChanged() || last_brightness == 0) {
    int brightness = volume_knob.getValue();

    DEBUG_PRINT("volume knob changed: ");
    DEBUG_PRINTLN(brightness);

    // TODO: we used to set 
    brightness = map(brightness, 0, 1023, min_brightness, max_brightness);

    if (brightness != last_brightness) {
      DEBUG_PRINT("new brightness: ");
      DEBUG_PRINTLN(brightness);

      // TODO: only call this if we are actually changing

      FastLED.setBrightness(brightness);

      last_brightness = brightness;
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
unsigned long loop_duration = 0;

void loop() {
  loop_duration = millis();

  setVisualizerBrightness();

  if (fft1024.available()) {
    updateFrequencies();

    // TODO: pass args to these functions instead of modifying globals
    mapFrequenciesToVisualizerMatrix();

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

    unsigned long actual_delay = millis();

    if (draw_delay >= long(draw_ms * 2)) {
      // Serial.print("Delaying for ");
      // Serial.print(draw_delay);
      // Serial.print(" + ");
      // Serial.println(draw_ms);

      FastLED.delay(draw_delay);
    } else {
      DEBUG_PRINT("Running too slow for dithering! ");
      DEBUG_PRINTLN(draw_ms * 2 - draw_delay);
      FastLED.show();
    }

    // TODO: we add 1 here just because if we are only 1 ms fast, we are fine
    actual_delay = millis() - actual_delay + 1;
    // Serial.print("actual delay: ");
    // Serial.println(actual_delay);

    // TODO: does delaying like this to keep an even framerate make sense?
    if (actual_delay < draw_delay + draw_ms) {
      // DEBUG_PRINT("delay to fix framerate: ");
      // DEBUG_PRINTLN(draw_delay + draw_ms - actual_delay);
      delay(draw_delay + draw_ms - actual_delay);
    }
  }
}
