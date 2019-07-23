#define DEBUG
// #define DEBUG_SERIAL_WAIT
#include "bs_debug.h"

#include <stdlib.h>

#include <Adafruit_MPR121.h>
#include <Audio.h>
#include <FastLED.h>
#include <FontMatrise.h>
#include <LEDMatrix.h>
#include <LEDSprites.h>
#include <LEDText.h>
#include <ResponsiveAnalogRead.h>
#include <SD.h>
#include <SPI.h>
#include <SerialFlash.h>
#include <Wire.h>

#include "config.h"

#if LIGHT_TYPE == DOTSTAR_MATRIX_64x8
  #pragma message "LIGHT_TYPE = dotstar matrix 2x 32x8"
  // TODO: MATRIX_CS_PIN if we plan on actually using the SD card
#elif LIGHT_TYPE == NEOPIXEL_MATRIX_2x_32x8
  #pragma message "LIGHT_TYPE = neopixel matrix 2x 32x8"
#else
  #error "unsupported LIGHT_TYPE"
#endif

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

cLEDMatrix<numLEDsX, numLEDsY, VERTICAL_ZIGZAG_MATRIX> text_matrix;
cLEDText ScrollingMsg;

cLEDMatrix<numLEDsX, numLEDsY, VERTICAL_ZIGZAG_MATRIX> sprite_matrix;
cLEDSprites Sprites(&sprite_matrix);

#define SHAPE_WIDTH    6
#define SHAPE_HEIGHT   6
const uint8_t ShapeData[] = 
{
  B8_1BIT(00110000),
  B8_1BIT(01001000),
  B8_1BIT(10000100),
  B8_1BIT(10000100),
  B8_1BIT(01001000),
  B8_1BIT(00110000),
};
struct CRGB ColorTable[1] = { CRGB(64, 128, 255) };
cSprite Shape(SHAPE_WIDTH, SHAPE_HEIGHT, ShapeData, 1, _1BIT, ColorTable, ShapeData);

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

// up to 12 touches all detected from one breakout board
// You can have up to 4 on one i2c bus
Adafruit_MPR121 cap = Adafruit_MPR121();
bool g_touch_available = false;
uint16_t g_current_touch = 0;
uint16_t g_last_touch = 0;
uint16_t g_changed_touch = 0;

bool g_flashlight_enabled = false;

// used to keep track of framerate // TODO: remove this if debug mode is disabled
unsigned long draw_micros = 0;
unsigned long last_update_micros = 0;
// unsigned long lastDraw = 0;

uint8_t g_brightness = 0, g_brightness_visualizer = 0, g_brightness_flashlight = 0;
bool g_dither = true;
// in order for dithering to work, we need to be able to FastLED.draw multiple times within a single frame
bool g_dither_works_with_framerate = true;

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
    Serial.printf("  i  low    Hz high    Hz\n");
    for (uint16_t b = 0; b < numFreqBands; b++) { // Test and print the bins from the calculated E
      n = pow(e, b);
      d = n + 0.5;

      Serial.printf("%3d ", b);

      Serial.printf("%4d ", count); // Print low bin
      Serial.printf("%5d ", count * FREQUENCY_RESOLUTION_HZ); // Print low bin Hz

      freqBands[b] = count;  // Save the low bin to a global

      count += d - 1;
      Serial.printf("%4d ", count); // Print high bin

      Serial.printf("%5d\n", (count + 1) * FREQUENCY_RESOLUTION_HZ); // Print high bin Hz

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
  // TODO: clock select pin for FastLED to OUTPUT like we do for the SDCARD?

  // do NOT turn off the built-in LED. it is tied to the audio board!

  #if LIGHT_TYPE == DOTSTAR_MATRIX_64x8
    Serial.println("Setting up dotstar 64x8 matrix...");
    // with pins 0/1 and 1500kHz data rate, this drew a single frame in ~8ms. faster rates crashed or flickered when the battery was low
    // with pins 14/7 and 4000kHz data rate, this drew a single frame in ~4ms. faster rates caused flickerin
    FastLED.addLeds<APA102, SPI_MOSI_PIN, SPI_SCK_PIN, BGR, DATA_RATE_KHZ(4000)>(leds[0], leds.Size()).setCorrection(TypicalSMD5050);
  #elif LIGHT_TYPE == NEOPIXEL_MATRIX_2x_32x8
    Serial.println("Setting up neopixel 2x 32x8 matrix...");
    // neopixels have a fixed data rate of 800kHz

    // serial output takes ~16.8ms
    // int half_size = leds.Size() / 2;
    // FastLED.addLeds<NEOPIXEL, MATRIX_DATA_PIN_1>(leds[0], half_size).setCorrection(TypicalSMD5050);
    // FastLED.addLeds<NEOPIXEL, MATRIX_DATA_PIN_2>(leds[half_size], half_size).setCorrection(TypicalSMD5050);

    // parallel output takes ~8.4ms
    // WS2811_PORTD: 2,14,7,8,6,20,21,5
    // WS2811_PORTC: 15,22,23,9,10,13,11,12,28,27,29,30 (these last 4 are pads on the bottom of the teensy)
    // WS2811_PORTDC: 2,14,7,8,6,20,21,5,15,22,23,9,10,13,11,12 - 16 way parallel
    FastLED.addLeds<WS2811_PORTD, 2>(leds[0], leds.Size() / 2);
  #else
    #error "unsupported LIGHT_TYPE"
  #endif

  // TODO: what should this be set to? the flexible panels are much larger
  // led matrix max is 15 amps, but because its flexible, best to keep it max of 5 amps. then we have 2 boards, so multiply by 2
  // the on/off switch only does 2 amps (and 2 amps is really bright)
  FastLED.setMaxPowerInVoltsAndMilliamps(3.7, 2000);
  // FastLED.setMaxPowerInVoltsAndMilliamps(5.0, 120); // when running through teensy's usb port, the max draw is much lower than with a battery

  // we use the volume knob to set the default brightness
  setBrightnessFromVolumeKnob();

  // TODO: default to brighter? (still max at 255 though)
  g_brightness_flashlight = g_brightness;

  FastLED.clear(true);

  // show red, green, blue, so that we make sure the lights are configured correctly
  Serial.println("Showing red...");
  colorPattern(CRGB::Red);

  // time FastLED.show so we can calculate maximum frame rate
  draw_micros = micros();
  FastLED.show();
  draw_micros = micros() - draw_micros;

  Serial.print("Draw time for show: ");
  Serial.print(draw_micros);
  Serial.println(" us");

  // TODO: calculate num_dither_shows based on brightness and visualizer_color_value

  // TODO: bring this back once we have ms_per_frame for sprite/text animations
  // TODO: maybe have a "min_ms_per_frame" for checking dither until then? 2x256 parallel neopixels with dithering run loop in 23ms
  // float ms_per_frame_needed_for_dither = draw_micros * num_dither_shows / 1000.0;
  // g_dither_works_with_framerate = (ms_per_frame_needed_for_dither <= ms_per_frame);
  // if (g_dither_works_with_framerate) {
  //   Serial.println("Dither works with framerate.");
  // } else {
  //   Serial.print("Dither does NOT work with framerate! Need ");
  //   Serial.print(ms_per_frame_needed_for_dither - ms_per_frame);
  //   Serial.println(" more ms");
  //   g_dither = false;
  //   FastLED.setDither(g_dither);
  // }

  // now delay for more time to make sure that fastled can power this many lights and update with this bandwidth
  FastLED.delay(1500 - draw_micros / 1000);

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
  randomSeed(analogRead(FLOATING_PIN));
  // TODO: use fastled's random function instead?

  #ifdef DEBUG
    Serial.println(random(100));
    Serial.println(random(100));
    Serial.println(random(100));
    Serial.println(random(100));
  #endif
}

void setupTouch() {
  pinMode(MPR121_IRQ, INPUT);

  g_touch_available = cap.begin(MPR121_ADDRESS);

  if (g_touch_available) {
    Serial.println("MPR121 found.");
  } else {
    Serial.println("MPR121 not found!");
    // TODO: print text on the LED matrix?
  }
}

void setupText() {
  ScrollingMsg.SetFont(MatriseFontData);

  ScrollingMsg.Init(&text_matrix, text_matrix.Width(), ScrollingMsg.FontHeight() + 1, 0, 0);

  ScrollingMsg.SetText((unsigned char *)text_woowoo, sizeof(text_woowoo) - 1);
  ScrollingMsg.SetScrollDirection(SCROLL_LEFT);
}

void setupSprites() {
  Shape.SetPositionFrameMotionOptions(
    0/*X*/, 
    0/*Y*/, 
    0/*Frame*/, 
    0/*FrameRate*/, 
    +1/*XChange*/, 
    2/*XRate*/, 
    +1/*YChange*/, 
    8/*YRate*/, 
    SPRITE_DETECT_EDGE | SPRITE_X_KEEPIN | SPRITE_Y_KEEPIN
  );
  Sprites.AddSprite(&Shape);
}

void setup() {
  debug_serial(115200, 2000);

  Serial.println("Setting up...");

  // setup SPI for the Audio board (which has an SD card reader)
  SPI.setMOSI(SPI_MOSI_PIN);
  SPI.setSCK(SPI_SCK_PIN);

  SPI.begin();

  setupSD();

  // right now, once we setup the lights, we can't use the SD card anymore
  // TODO: add a CS pin for the lights
  setupLights();

  setupTouch();

  setupAudio();

  setupFFTBins();

  setupText();

  setupSprites();

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

  float overall_max = minMaxLevel;

  for (uint16_t i = 0; i < numFreqBands; i++) {
    if (i < numFreqBands - 1) {
      frequencies[i].current_magnitude = fft1024.read(freqBands[i], freqBands[i + 1] - 1);
    } else {
      // the last level always goes to maxBin
      frequencies[i].current_magnitude = fft1024.read(freqBands[numFreqBands - 1], maxBin);
    }

    if (frequencies[i].current_magnitude > frequencies[i].max_magnitude) {
      frequencies[i].max_magnitude = frequencies[i].current_magnitude;
    }

    if (frequencies[i].max_magnitude < minMaxLevel) {
      // don't let the max ever go to zero so that it turns off when its quiet instead of activating at a whisper
      frequencies[i].max_magnitude = minMaxLevel;
    } else if (frequencies[i].max_magnitude > overall_max) {
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
}

// TODO: args instead of globals
void mapFrequenciesToVisualizerMatrix() {
  // shift increments every current_ms_per_shift milliseconds and is used to slowly modify the pattern
  static uint16_t shift = 0;
  static uint8_t ms_per_shift_index = 0;
  static uint16_t current_ms_per_shift = ms_per_shift[ms_per_shift_index];
  static unsigned long next_shift_at_ms = 0;
  // cycle between shifting up and shifting down
  static bool reverse_rotation = true;
  // static unsigned long next_change_ms_per_shift = 0;

  // cycle between lights coming from the top and the bottom
  static bool should_flip_y[visualizerNumLEDsX] = {false};
  static uint16_t map_visualizer_y[visualizerNumLEDsY] = {0};
  static bool flip_y = false;
  static bool new_pattern = true;

  static uint8_t last_frame_height[visualizerNumLEDsX] = {0};
  static uint8_t lowestIndexToLight = 1;  // 0 is the border
  static uint8_t lowestIndexToLightWhite = 3;

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
    // TODO: more patterns. maybe one where it grows from the middle. or goes from the top and the bottom

    // flip the bottom and the top
    for (uint8_t y = 0; y < visualizerNumLEDsY; y++) {
      map_visualizer_y[y] = 7 - y;
    }

    new_pattern = false;
  }

  CHSV visualizer_white = CHSV(0, 0, visualizer_white_value);

  for (uint8_t x = 0; x < visualizerNumLEDsX; x++) {
    // we take the absolute value because shift might negative
    uint8_t shifted_x = abs((x + shift) % visualizerNumLEDsX);

    uint8_t visualizer_hue = map(x, 0, visualizerNumLEDsX - 1, 0, 255);

    // TODO: color palettes instead of simple rainbow hue
    // TODO: variable brightness? variable saturation?
    CHSV visualizer_color = CHSV(visualizer_hue, 255, visualizer_color_value);

    uint8_t i = visualizerXtoFrequencyId[x];

    // draw a border
    if (g_flashlight_enabled) {
      visualizer_matrix(shifted_x, 0) = visualizer_white;
      visualizer_matrix(shifted_x, visualizerNumLEDsY - 1) = visualizer_white;
    } else {
      visualizer_matrix(shifted_x, 0) = visualizer_color;
      visualizer_matrix(shifted_x, visualizerNumLEDsY - 1) = visualizer_color;
    }

    if (i < numFreqBands && frequencies[i].average_scaled_magnitude > 0) {
      // use the value to calculate the height for this color
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

            last_frame_height[x] = 0;
          } else {
            // taller bars should have white at the top
            // but only if they are the same height or taller than they were on the previous frame. this way shrinking bars are topped by colors
            if (highestIndexToLight >= last_frame_height[x]) {
              visualizer_matrix(shifted_x, shifted_y) = visualizer_white;

              last_frame_height[x] = highestIndexToLight;
            } else {
              visualizer_matrix(shifted_x, shifted_y) = visualizer_color;

              // todo: this is probably wrong
              last_frame_height[x] = highestIndexToLight + 1;
            }
          }

          if (highestIndexToLight >= visualizerNumLEDsY - 1) {
            // loud_frame_counter++;
            // increment_loud_frame_counter = true;

            uint8_t r = random(100);

            // TODO: this doesn't work as well with the bars being two wide. need configurable 
            if (r < 34) {
              EVERY_N_SECONDS(3) {
                // TODO: instead of a hard rotate, cycle speeds
                reverse_rotation = !reverse_rotation; // TODO: enum instead of bool?
                next_shift_at_ms = millis() + current_ms_per_shift;
              }
            } else if (r < 67) {
              // TODO: maybe instead of EVERY_N_SECONDS, have a timer for each x?
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
        // TODO: fading looks bad since they all fade at even rate and we want the top light to turn off first
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

  if (millis() >= next_shift_at_ms) {
    // DEBUG_PRINT("SHIFTED! now: ");
    // DEBUG_PRINT(millis());
    // DEBUG_PRINT(" ms; goal: ");
    // DEBUG_PRINT(next_shift_at_ms);
    // DEBUG_PRINT(" ms; diff: ");
    // DEBUG_PRINT(millis() - next_shift_at_ms);
    // DEBUG_PRINT(" ms; next in: ");
    // DEBUG_PRINTLN(current_ms_per_shift);

    next_shift_at_ms = millis() + current_ms_per_shift;

    if (reverse_rotation) {
      shift--;
    } else {
      shift++;
    }
  }

  // TODO: debug timer
}

bool setBrightnessFromTouch() {
  bool brightness_changed = false;

  if (g_changed_touch & _BV(brim_left) && g_current_touch & _BV(brim_left)) {
    // hold the brim front and tap brim_left to increase brightness
    if (g_brightness < max_brightness) {
      brightness_changed = true;

      g_brightness++;

      DEBUG_PRINT("Brightness increased to ");
      DEBUG_PRINTLN(g_brightness);

      FastLED.setBrightness(g_brightness);
    } else {
      DEBUG_PRINTLN("Brightness @ max");
    }
  } else if (g_changed_touch & _BV(brim_back) && g_current_touch & _BV(brim_back)) {
    // hold the brim front and tap brim_back to decrease brightness
    if (g_brightness > min_brightness) {
      brightness_changed = true;

      g_brightness--;

      DEBUG_PRINT("Brightness decreased to ");
      DEBUG_PRINTLN(g_brightness);

      FastLED.setBrightness(g_brightness);
    } else {
      DEBUG_PRINTLN("Brightness @ min");
    }
  } else {
    // TODO: if brim_front is held for 5 seconds without other touches, toggle flashlight mode
  }

  if (brightness_changed) {
    if (g_flashlight_enabled) {
      g_brightness_flashlight = g_brightness;
    } else {
      g_brightness_visualizer = g_brightness;
    }
  }

  return brightness_changed;
}

bool setBrightnessFromVolumeKnob() {
  bool brightness_changed = false;

  EVERY_N_MILLIS(100) {
    volume_knob.update();
  }

  if (volume_knob.hasChanged() || g_brightness == 0) {
    int knob_value = volume_knob.getValue();

    // TODO: we used to set 
    uint8_t brightness = map(knob_value, 0, 1023, min_brightness, max_brightness);

    if (brightness != g_brightness) {
      brightness_changed = true;

      DEBUG_PRINT("volume knob changed: ");
      DEBUG_PRINT(knob_value);
      DEBUG_PRINT("; new brightness: ");
      DEBUG_PRINTLN(brightness);

      // TODO: only call this if we are actually changing

      // split brightness doesn't work with the knob
      g_brightness = brightness;

      FastLED.setBrightness(g_brightness);

      if (g_dither_works_with_framerate) {
        bool dither = (brightness >= dither_brightness_cutoff);
        if (dither != g_dither) {
          g_dither = dither;

          FastLED.setDither(g_dither);
        }
      }
    }
  }

  return brightness_changed;
}

void combineMatrixes() {
  // TODO: what should we do here? how should we overlay/interleave the different matrixes into one?

  // TODO: this could probably be a lot more efficient
  for (uint16_t x = 0; x < numLEDsX; x++) {
    for (uint16_t y = 0; y < numLEDsY; y++) {
      // if text/sprite, display it
      // TODO: how are masks vs off going to be detected?
      // TODO: do we need a margin? maybe if text is scrolling skip the visualizer 
      // TODO: do we want the text to scroll at the same rate as the visualizer?
      // TODO: text_matrix OR sprite_matrix
      if (text_matrix(numLEDsX - x, y)) {
        leds(x, y) = text_matrix(numLEDsX - x, y);
      } else if (sprite_matrix(numLEDsX - x, y)) {
        leds(x, y) = sprite_matrix(numLEDsX - x, y);
      } else {
        // TODO: else display visualizer (wrapping on the x axis)
        if (y < visualizerNumLEDsY) {
          uint16_t vis_x = x % visualizerNumLEDsX;

          leds(x, y) = visualizer_matrix(vis_x, y);
        } else {
          leds(x, y) = CRGB::Black;
        }
      }
    }
  }

  // TODO: return false if nothing changed? we can skip drawing then
}


void loop() {
  static bool new_frame = false;
  static bool text_complete = false;

  if (g_touch_available) {
    // if IRQ is low, there is new touch data to read
    if (digitalRead(MPR121_IRQ) == LOW) {
      g_current_touch = cap.touched();

      g_changed_touch = g_current_touch ^ g_last_touch;

      if (setBrightnessFromTouch()) {
        // we can wait for a next audio frame
        // new_frame = true;
      };

      // TODO: do more things based on touch
      // TODO: toggleFlashLightFromTouch();
        // ScrollingMsg.SetText((unsigned char *)text_flashlight, sizeof(text_flashlight) - 1);
        // text_complete = false;

      if (text_complete) {
        // TODO: check touch for button to trigger scrolling text
      }

      g_last_touch = g_current_touch;
    }
  } else {
    if (setBrightnessFromVolumeKnob()) {
      // this floats a little and causes draws when we don't really care
      // we can wait for a next audio frame
      // new_frame = true;
    }
  }

  if (fft1024.available()) {
    updateFrequencies();

    // TODO: pass args to these functions instead of modifying globals
    mapFrequenciesToVisualizerMatrix();

    new_frame = true;
  }

  if (!text_complete) {
    EVERY_N_MILLIS(1000/25) {
      // draw text
      int scrolling_ret = ScrollingMsg.UpdateText();
      // DEBUG_PRINT("Scrolling ret: ");
      // DEBUG_PRINTLN(scrolling_ret);
      if (scrolling_ret == -1) {
        // when UpdateText returns -1, there is no more text to display
        text_complete = true;
      } else {
        // UpdateText drew a new frame
        new_frame = true;

        if (scrolling_ret == 1) {
          // when UpdateText returns 1, "FLASHLIGHT" text and a delay is done being displayed
          // toggle flashlight mode
          g_flashlight_enabled = !g_flashlight_enabled;

          if (g_brightness_flashlight) {
            g_brightness = g_brightness_flashlight;
          } else if (g_brightness_visualizer) {
            g_brightness = g_brightness_visualizer;
          }

          // TODO: add a spinning white light in the front with sprites
        }
        // TODO: scrolling_ret to enable/disable sprites
      }
    }
  } else {
    EVERY_N_SECONDS(120) {
      // scroll text again
      // TODO: cycle between different text
      // TODO: instead of every_n_seconds, tie to touch sensor and to a bunch of visualizer columns hitting the top in a single frame
      // TODO: put this back to text_woowoo
      ScrollingMsg.SetText((unsigned char *)text_woowoo, sizeof(text_woowoo) - 1);

      ScrollingMsg.UpdateText();

      text_complete = false;
      new_frame = true;
    }
  }

  // draw sprites
  EVERY_N_MILLIS(1000/25) {
    fill_solid(sprite_matrix[0], sprite_matrix.Size(), CRGB::Black);

    Sprites.UpdateSprites();

    // TODO: do collision detection for pacman
    //Sprites.DetectCollisions();

    Sprites.RenderSprites();

    new_frame = true;
  }

  if (new_frame) {
    combineMatrixes();

    // if dithering is off, we can run at a faster framerate
    if (g_dither) {
      FastLED.delay((draw_micros * num_dither_shows) / 1000);
    } else {
      FastLED.show();
    }

    // debug print
    #ifdef DEBUG
      for (uint16_t i = 0; i < numFreqBands; i++) {
        Serial.print("| ");

        // TODO: maybe do something with parity here? 
        // do some research

        if (frequencies[i].average_scaled_magnitude > 0) {
          Serial.print(frequencies[i].average_scaled_magnitude / 255.0, 2);
        } else {
          Serial.print("    ");
        }
      }
      Serial.print("| ");
      Serial.print(AudioMemoryUsageMax());
      Serial.print(" blocks | ");

      Serial.print(g_brightness);
      Serial.print(" bright | ");

      // finish debug print
      Serial.print(micros() - last_update_micros);
      Serial.println(" us");
      last_update_micros = micros();
      Serial.flush();
    #endif

    new_frame = false;
  }
}
