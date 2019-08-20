#define DEBUG
#define DEBUG_VERBOSE
// #define DEBUG_SERIAL_WAIT
// TODO: publish this
#include <bs_debug.h>

#include <stdlib.h>

#include <Adafruit_MPR121.h>
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

#include "FontBS.h"
#include "config.h"

uint16_t freqBands[numFreqBands];

// keep track of the current levels. this is a sum of multiple frequency bins.
// keep track of the max volume for each frequency band (slowly decays)
struct frequency {
  float current_magnitude;
  float ema_magnitude;
  // float ema_db_spl;
  uint8_t averaged_scaled_magnitude; // exponential moving average of current_magnitude divided by overall max_magnitude
  uint8_t level;  // TODO: name this better. its the highest index we are lighting in the visualizer matrix. it shouldn't be on this struct either
  unsigned long nextChangeMs;  // keep track of when we turned a light on so they don't flicker when we change them
};
frequency frequencies[numFreqBands] = {0, 0, 0, 0, 0};

float g_highest_current_magnitude = 0;
float g_highest_ema_magnitude = 0;
float g_highest_max_magnitude = 0;

#ifdef OUTPUT_LED_MATRIX
  // TODO: move this to a seperate file so that we can support multiple led/el light combinations
  cLEDMatrix<visualizerNumLEDsX, visualizerNumLEDsY, VERTICAL_ZIGZAG_MATRIX> visualizer_matrix;

  cLEDMatrix<numLEDsX, numLEDsY, VERTICAL_ZIGZAG_MATRIX> text_matrix;
  cLEDText ScrollingMsg;

  // cLEDMatrix<numLEDsX, numLEDsY, VERTICAL_ZIGZAG_MATRIX> sprite_matrix;
  // cLEDSprites Sprites(&sprite_matrix);

  // #define SHAPE_FRAMES   4
  // #define SHAPE_WIDTH    6
  // #define SHAPE_HEIGHT   6
  // const uint8_t ShapeData[] = 
  // {
  //   // frame 0
  //   B8_1BIT(00110000),
  //   B8_1BIT(01001000),
  //   B8_1BIT(10000100),
  //   B8_1BIT(10100100),
  //   B8_1BIT(01001000),
  //   B8_1BIT(00110000),
  //   // frame 1
  //   B8_1BIT(00110000),
  //   B8_1BIT(01001000),
  //   B8_1BIT(10100100),
  //   B8_1BIT(10000100),
  //   B8_1BIT(01001000),
  //   B8_1BIT(00110000),
  //   // frame 2
  //   B8_1BIT(00110000),
  //   B8_1BIT(01001000),
  //   B8_1BIT(10010100),
  //   B8_1BIT(10000100),
  //   B8_1BIT(01001000),
  //   B8_1BIT(00110000),
  //   // frame 3
  //   B8_1BIT(00110000),
  //   B8_1BIT(01001000),
  //   B8_1BIT(10000100),
  //   B8_1BIT(10010100),
  //   B8_1BIT(01001000),
  //   B8_1BIT(00110000),
  // };
  // struct CRGB ColorTable[1] = { CRGB(64, 128, 255) };
  // cSprite Shape(SHAPE_WIDTH, SHAPE_HEIGHT, ShapeData, SHAPE_FRAMES, _1BIT, ColorTable, ShapeData);

  // the text and sprites and visualizer get combined into this
  cLEDMatrix<numLEDsX, numLEDsY, VERTICAL_ZIGZAG_MATRIX> leds;
#elif LIGHT_TYPE == EL_WIRE_8
  // TODO: support multiple el sequencers
  unsigned char el_output[1] = {0};
#else
  #error WIP
#endif

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

// TODO: only do this if there is a define saying to
// up to 12 touches all detected from one breakout board
// You can have up to 4 on one i2c bus
Adafruit_MPR121 cap = Adafruit_MPR121();
bool g_touch_available = false;
uint16_t g_current_touch = 0;
uint16_t g_last_touch = 0;
uint16_t g_changed_touch = 0;

enum flashlight_state {
  off,
  on,
};
flashlight_state g_flashlight_state = off;

// used to keep track of framerate // TODO: remove this if debug mode is disabled
unsigned long draw_micros = 0;
unsigned long last_update_micros = 0;
// unsigned long lastDraw = 0;

// TODO: only do this if we are using FastLED
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

#ifdef OUTPUT_LED_MATRIX
  void colorPattern(CRGB::HTMLColorCode color) {
    // TODO: sin wave
    for (uint8_t x = 0; x < numLEDsX; x++) {
      uint8_t y = x % numLEDsY;
      leds(x, y) = color;
    }
  }
#endif

void setupLights() {
  #ifdef OUTPUT_LED_MATRIX
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
    // sometimes the first read is 0, so give it multiple tries to wake up
    setBrightnessFromVolumeKnob();
    delay(100);
    setBrightnessFromVolumeKnob();
    delay(100);
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

    #ifdef DEBUG
      // now delay for more time to make sure that fastled can power this many lights and update with this bandwidth
      FastLED.delay(1500 - draw_micros / 1000);

      Serial.println("Showing green...");
      colorPattern(CRGB::Green);
      FastLED.delay(1500);

      Serial.println("Showing blue...");
      colorPattern(CRGB::Blue);
      FastLED.delay(1500);
    #endif

    FastLED.clear(true);
    FastLED.show();
  #elif LIGHT_TYPE == EL_WIRE_8
    // https://www.pjrc.com/teensy/td_uart.html
    Serial1.begin(9600);  // RX=0 (yellow), TX=1 (green)
    // TODO: if we want more Serial outputs, they will have to be bit-banged. Audio shield uses the pins we need for Serial2 and Serial3

    Serial.print("Waiting for Serial1... ");
    while (!Serial1) {
      ; // wait for serial port to connect
    }
    Serial.println("DONE");
  #endif
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
  // randomSeed takes unsigned long, but analogRead only gives an int
  unsigned long seed = 0;

  // TODO: how many times? we aren't using this for crypto, so once was probably fine
  // someone said analogReady only gives 1-2 bits of entropy
  // another person said we should only care about the lowest byte
  for (int i = 0; i < 256; i++) {
    unsigned long seed_temp = 0;
    for (int i = 0; i < __SIZEOF_LONG__ / __SIZEOF_INT__; i++) {
      seed_temp |= analogRead(FLOATING_PIN)<<(i * __SIZEOF_INT__ * 8);
    }
    seed ^= seed_temp;
  }

  // use arduino's random to seed fastled's random
  // FastLED's random is "significantly faster than Arduino random(), but also somewhat less random"
  randomSeed(seed);

  random16_add_entropy(random(UINT32_MAX));

  #ifdef DEBUG
    Serial.print("Random: ");
    Serial.println(random(100));

    Serial.print("Random: ");
    Serial.println(random(100));

    Serial.print("Random: ");
    Serial.println(random(100));

    Serial.print("Random: ");
    Serial.println(random(100));

    Serial.print("Random8: ");
    Serial.println(random8(100));

    Serial.print("Random8: ");
    Serial.println(random8(100));

    Serial.print("Random8: ");
    Serial.println(random8(100));

    Serial.print("Random8: ");
    Serial.println(random8(100));
  #endif
}

#ifdef INPUT_TOUCH
  void setupTouch() {
    pinMode(MPR121_IRQ, INPUT);

    g_touch_available = cap.begin(MPR121_ADDRESS);

    if (g_touch_available) {
      Serial.println("MPR121 found.");

      // cap.setThresholds(12, 6);  // this is the default
      cap.setThresholds(48, 24);

    } else {
      Serial.println("MPR121 not found!");
      // TODO: print text on the LED matrix?
    }
  }
#endif

#ifdef OUTPUT_LED_MATRIX
  void setupMatrixText() {
    ScrollingMsg.SetFont(BSFontData);

    ScrollingMsg.Init(&text_matrix, text_matrix.Width(), ScrollingMsg.FontHeight() + 1, 0, 1);

    ScrollingMsg.SetScrollDirection(SCROLL_LEFT);

    setText(debug);
  }


  ScrollingText nextCheer() {
    static ScrollingText next_cheer = (ScrollingText)(random8(CHEER + 1, CHEER_END - 1)); 

    ScrollingText result = next_cheer;

    next_cheer = (ScrollingText)(next_cheer + 1);
    if (next_cheer >= CHEER_END) {
      // TODO: randomize the order every loop. maybe shuffle 4 "decks" of words together so they can sometimes double up
      next_cheer = (ScrollingText)(CHEER + 1);
    }

    return result;
  }

  void setText(ScrollingText text) {
    if (text == CHEER || text == CHEER_END) {
      text = nextCheer();
    }

    // TODO: make this optional?
    fill_solid(text_matrix[0], text_matrix.Size(), CRGB::Black);

    g_scrolling_text = text;

    unsigned char *text_chars;
    unsigned long text_len;

    // i miss rust's match statement
    if (text == none) {
      text_chars = (unsigned char *)text_woo1;
      text_len = 0;
    } else if (text == debug) {
      text_chars = (unsigned char *)text_debug;
      text_len = sizeof(text_debug);
    } else if (text == flashlight) {
      text_chars = (unsigned char *)text_flashlight;
      text_len = sizeof(text_flashlight) - 1;
    } else if (text == woo1) {
      text_chars = (unsigned char *)text_woo1;
      text_len = sizeof(text_woo1) - 1;
    } else if (text == woo2) {
      text_chars = (unsigned char *)text_woo2;
      text_len = sizeof(text_woo2) - 1;
    } else if (text == party) {
      text_chars = (unsigned char *)text_party;
      text_len = sizeof(text_party) - 1;
    } else if (text == dance) {
      text_chars = (unsigned char *)text_dance;
      text_len = sizeof(text_dance) - 1;
    } else if (text == gambino) {
      text_chars = (unsigned char *)text_gambino;
      text_len = sizeof(text_gambino) - 1;
    } else {
      Serial.print("ERROR! Missed handling a ScrollingText enum ");
      Serial.println(text);
      return;
    }

    ScrollingMsg.SetText(text_chars, text_len);
  }

  // void setupSprites() {
  //   // sprites run at 60fps
  //   Shape.SetPositionFrameMotionOptions(
  //     0/*X*/, 
  //     0/*Y*/, 
  //     0/*Frame*/, 
  //     8/*FrameRate*/, 
  //     +1/*XChange*/, 
  //     8/*XRate*/, 
  //     +1/*YChange*/, 
  //     16/*YRate*/, 
  //     SPRITE_DETECT_EDGE | SPRITE_X_KEEPIN | SPRITE_Y_KEEPIN
  //   );
  //   Sprites.AddSprite(&Shape);
  // }
#endif

void setup() {
  debug_serial(115200, 2000);

  Serial.println("Setting up...");

  // setup SPI for the Audio board (which has an SD card reader)
  SPI.setMOSI(SPI_MOSI_PIN);
  SPI.setSCK(SPI_SCK_PIN);

  SPI.begin();

  setupSD();

  // right now, once we setup an LED matrix, we can't use the SD card anymore
  // TODO: add a CS pin for the lights
  setupLights();

  #ifdef INPUT_TOUCH
    setupTouch();
  #endif

  setupAudio();

  setupFFTBins();

  // setupMatrixLevels();

  setupRandom();

  #ifdef OUTPUT_LED_MATRIX
    setupMatrixText();

    // setupSprites();
  #endif

  Serial.println("Starting...");
}

// we could/should pass fft and level as args
void updateLevelsFromFFT() {
  // https://forum.pjrc.com/threads/32677-Is-there-a-logarithmic-function-for-FFT-bin-selection-for-any-given-of-bands

  // read the FFT frequencies into numOutputs levels
  // music is heard in octaves, but the FFT data
  // is linear, so for the higher octaves, read
  // many FFT bins together.

  float highest = 0;

  for (uint16_t i = 0; i < numFreqBands; i++) {
    if (i < numFreqBands - 1) {
      frequencies[i].current_magnitude = fft1024.read(freqBands[i], freqBands[i + 1] - 1);
    } else {
      // the last level always goes to maxBin
      frequencies[i].current_magnitude = fft1024.read(freqBands[numFreqBands - 1], maxBin);
    }

    highest = max(highest, frequencies[i].current_magnitude);
  }

  g_highest_current_magnitude = highest;
}

void updateFrequencies() {
  // read FFT frequency data into a bunch of levels. assign each level a color and a brightness
  updateLevelsFromFFT();

  // exponential moving average
  float up_alpha = 0.98;  // if going up, give the new measure a heavy weight
  float down_alpha = 0.98;  // TODO: if going down...
  float alphaScale = 1.0;

  float highest_ema_magnitude = 0;

  for (uint16_t i = 0; i < numFreqBands; i++) {

    // TODO: don't scale linearly. sounds have to have a lot more energy in order to sound twice as loud
    // TODO: instead of scaling to an overall max. scale to some average of the overall and this frequency's max
    float current_reading = frequencies[i].current_magnitude;

    // cut off the bottom
    // TODO: learn more about decibles and phons. do something fancier
    // scaled_reading = map(scaled_reading, 0.0, 1.0, -0.3, 1.0);

    float last_reading = frequencies[i].ema_magnitude;

    if (current_reading < last_reading) {
      frequencies[i].ema_magnitude = (down_alpha * current_reading + (alphaScale - down_alpha) * last_reading) / alphaScale;
    } else {
      frequencies[i].ema_magnitude = (up_alpha * current_reading + (alphaScale - up_alpha) * last_reading) / alphaScale;
    }

    // TODO: is this right? the pjrc forum that I got this function from had a comment about using 94 for db spl, but it doesn't feel right to me
    // frequencies[i].ema_db_spl = db(frequencies[i].ema_magnitude, 94);

    // TODO: convert db_spl to phon or some other curve to match human hearing 

    highest_ema_magnitude = max(highest_ema_magnitude, frequencies[i].ema_magnitude);
  }

  g_highest_ema_magnitude = highest_ema_magnitude;

  // g_highest_max_magnitude = max(previous frame's g_highest_max_magnitude * decayMax, g_highest_ema_magnitude, minMaxLevel)
  // g_highest_max_magnitude = max(g_highest_max_magnitude, g_highest_current_magnitude); // skip this! on loud sounds, it is always >ema which means we never get 1.0 for avg_scaled_magnitude
  g_highest_max_magnitude = max(g_highest_max_magnitude * decayMax, g_highest_ema_magnitude);
  g_highest_max_magnitude = max(g_highest_max_magnitude, minMaxLevel);

  float activate_threshold = g_highest_max_magnitude * activate_difference;

  for (uint16_t i = 0; i < numFreqBands; i++) {
    if (frequencies[i].ema_magnitude >= activate_threshold) {
      // TODO: include log here somehow. maybe convert to decibel spl?
      // frequencies[i].averaged_scaled_magnitude = 10 * log10f(frequencies[i].ema_magnitude / g_highest_max_magnitude);
      frequencies[i].averaged_scaled_magnitude = constrain(frequencies[i].ema_magnitude / g_highest_max_magnitude, 0.0f, 1.0f) * 255;
    } else {
      frequencies[i].averaged_scaled_magnitude = 0;
    }
  }
}

// TODO: args instead of globals
void mapFrequenciesToOutputBuffer() {
  #ifdef OUTPUT_LED_MATRIX
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
    static uint8_t lowestIndexToLightWhite = 4;// visualizerNumLEDsY - 1;

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

    CRGB visualizer_white = CRGB(CHSV(0, 0, visualizer_white_value));

    for (uint8_t x = 0; x < visualizerNumLEDsX; x++) {
      // we take the absolute value because shift might negative
      uint8_t shifted_x = abs((x + shift) % visualizerNumLEDsX);

      uint8_t visualizer_hue = map(x, 0, visualizerNumLEDsX - 1, 0, 255);

      // TODO: color palettes instead of simple rainbow hue
      // TODO: variable brightness? variable saturation?
      CHSV visualizer_color = CHSV(visualizer_hue, 255, visualizer_color_value);

      uint8_t i = visualizerXtoFrequencyId[x];

      // draw a border
      visualizer_matrix(shifted_x, 0) = visualizer_color;
      if (g_flashlight_state == flashlight_state::on && shifted_x % 2 == 1) {
        // visualizer_matrix(shifted_x, 0) = visualizer_white;
        visualizer_matrix(shifted_x, visualizerNumLEDsY - 1) = visualizer_white;
      } else {
        visualizer_matrix(shifted_x, visualizerNumLEDsY - 1) = visualizer_color;
      }

      // if this column is on or should be turned on
      if (i < numFreqBands && (frequencies[i].level > 1 || frequencies[i].averaged_scaled_magnitude > 0)) {
        // use the averaged_scaled_magnitude to calculate the height for this color
        // TODO: this should be an exponential scale. i think we can use findE()
        uint8_t highestIndexToLight = map(frequencies[i].averaged_scaled_magnitude, UINT8_MIN, UINT8_MAX, 0, visualizerNumLEDsY - 1);

        // TODO: these should be on a different struct dedicated to the matrix
        if (highestIndexToLight != frequencies[i].level) {
          bool level_changed = true;
          if (highestIndexToLight > frequencies[i].level) {
            // if the bar is growing...

            if (millis() < frequencies[i].nextChangeMs) {
              // nevermind! we need to wait longer before changing this in order to reduce flicker
              highestIndexToLight = frequencies[i].level;
              level_changed = false;
            }
          } else {
            // if the bar is shrinking, limit to shrinking 1 level per X ms...
            if (millis() < frequencies[i].nextChangeMs) {
              // nevermind! we need to wait longer before changing this in order to reduce flicker
              highestIndexToLight = frequencies[i].level;
              level_changed = false;
            } else {
              highestIndexToLight = frequencies[i].level - 1;
            }
          }

          if (level_changed) {
            frequencies[i].level = highestIndexToLight;

            // the level has changed! set timer to prevent flicker
            // TODO: two timers so that lights can turn off slower than they turn on?
            frequencies[i].nextChangeMs = millis() + minOnMs;
          }
        }

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

              uint8_t r = random8(100);

              // TODO: this doesn't work as well with the bars being two wide. need configurable 
              if (r < 34) {
                EVERY_N_SECONDS(3) {
                  // TODO: instead of a hard rotate, cycle speeds
                  reverse_rotation = !reverse_rotation; // TODO: enum instead of bool?
                  next_shift_at_ms = millis() + current_ms_per_shift;
                }
              } else if (r < 67) {
                // TODO: maybe instead of EVERY_N_SECONDS, have a timer for each x?
                EVERY_N_SECONDS(6) {
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

        if (i < numFreqBands) {
          if (millis() < frequencies[i].nextChangeMs) {
            // nevermind! we need to wait longer to reduce flicker
            // TODO: we might not need this
            continue;
          }

          frequencies[i].level = 0;
        }

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

    // TODO: debug timer?
  #elif LIGHT_TYPE == EL_WIRE_8
    // TODO: better support multiple sequencers
    // TODO: have 2 or 3 wires per frequency and turn more/less of them on
    // TODO: use maxOn. need to do two loops then. one to turn off, another to turn on
    for (int i = 0; i < numFreqBands; i++) {

      bool should_be_on = frequencies[i].averaged_scaled_magnitude >= UINT8_MAX * activate_difference;

      if (should_be_on != frequencies[i].level) {
        bool level_changed = true;
        if (should_be_on) {
          // if the wire is supposed to be on...

          if (millis() < frequencies[i].nextChangeMs) {
            // nevermind! we need to wait longer before changing this in order to reduce flicker
            should_be_on = frequencies[i].level;
            level_changed = false;
          }
        } else {
          // if the wire is supposed to be off...
          if (millis() < frequencies[i].nextChangeMs) {
            // nevermind! we need to wait longer before changing this in order to reduce flicker
            should_be_on = frequencies[i].level;
            level_changed = false;
          }
        }

        if (level_changed) {
          frequencies[i].level = should_be_on;

          // the level has changed! set timer to prevent flicker
          // TODO: two timers so that lights can turn off slower than they turn on?
          frequencies[i].nextChangeMs = millis() + minOnMs;
        }
      }

      if (frequencies[i].level > 0) {
        bitSet(el_output[i / 8], i % 8);
      } else {
        bitClear(el_output[i / 8], i % 8);
      }
    }
  #endif
}

#ifdef INPUT_TOUCH
  bool setThingsFromTouch() {
    bool brightness_changed = false;

    // TODO: zfill
    DEBUG_PRINT("changed touch: ");
    DEBUG_PRINTLN2(g_changed_touch, BIN);
    DEBUG_PRINT("current touch: ");
    DEBUG_PRINTLN2(g_current_touch, BIN);

    if (g_changed_touch & _BV(brim_right) && g_current_touch & _BV(brim_right)) {
      // TODO: require another input to be held?
      // tap brim_left to increase brightness
      // TODO: this is too slow. allow holding to continue decreasing
      if (g_brightness < max_brightness) {
        brightness_changed = true;

        g_brightness++;

        DEBUG_PRINT("Brightness increased to ");
        DEBUG_PRINTLN(g_brightness);

        FastLED.setBrightness(g_brightness);
      } else {
        DEBUG_PRINTLN("Brightness @ max");
      }
    } else if (g_changed_touch & _BV(brim_left) && g_current_touch & _BV(brim_left)) {
      // TODO: require another input to be held?
      // tap brim_right to decrease brightness
      // TODO: this is too slow. allow holding to continue decreasing
      if (g_brightness > min_brightness) {
        brightness_changed = true;

        g_brightness--;

        DEBUG_PRINT("Brightness decreased to ");
        DEBUG_PRINTLN(g_brightness);

        FastLED.setBrightness(g_brightness);
      } else {
        DEBUG_PRINTLN("Brightness @ min");
      }
    } else if (g_changed_touch & _BV(brim_front) && g_current_touch & _BV(brim_front)) {
      // tap brim_front to toggle flashlight
      // TODO: make it so we have to hold it down for a moment
      if (g_scrolling_text == flashlight) {
        // the button was pressed while we were already scrolling "flashlight". cancel the currently scrolling text
        DEBUG_PRINTLN("Stopping flashlight text");
        setText(none);
      } else {
        // start scrolling "flashlight"
        // the flashlight will toggle once the message is done scrolling
        DEBUG_PRINTLN("Starting flashlight text");
        setText(flashlight);
      }
    } else if (g_changed_touch & _BV(top) && g_current_touch & _BV(top)) {
      if (g_scrolling_text == none) {
        DEBUG_PRINTLN("Starting a cheer because the top was touched");
        setText(CHEER);
      } else {
        DEBUG_PRINT("Skipping cheer while other text is scrolling ");
        DEBUG_PRINTLN(g_scrolling_text);
      }
    } else {
      if (g_current_touch != 0) {
        DEBUG_PRINTLN("unimplemented touches detected");
      }
    }

    if (brightness_changed) {
      // save different brightness for flashlight and visualizer
      if (g_flashlight_state == on) {
        g_brightness_flashlight = g_brightness;
      } else {
        g_brightness_visualizer = g_brightness;
      }
    }

    // TODO: check touch for button to trigger scrolling text
    // if (g_scrolling_text == none) {
    // }

    return brightness_changed;
  }
#endif

#ifdef OUTPUT_LED
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
#endif

#ifdef OUTPUT_LED_MATRIX
  void combineMatrixes() {
    // TODO: what should we do here? how should we overlay/interleave the different matrixes into one?

    static CHSV visualizer_white = CHSV(0, 0, visualizer_white_value);
    static CRGB black = CRGB::Black;

    // update the value in case it changed
    visualizer_white.value = visualizer_white_value;

    // TODO: this could probably be a lot more efficient
    for (uint16_t x = 0; x < numLEDsX; x++) {
      uint16_t vis_x = x % visualizerNumLEDsX;

      for (uint16_t y = 0; y < numLEDsY; y++) {
        // if visualizer is white, display it
        // else if text, display it
        // else if sprite, display it
        // else display the visualizer

        // TODO: how are masks vs off going to be detected?
        // TODO: text_matrix OR sprite_matrix
        if (y < visualizerNumLEDsY && visualizer_matrix(vis_x, y) == visualizer_white) {
          leds(x, y) = visualizer_white;
        } else if (g_scrolling_text != none && text_matrix(numLEDsX - 1 - x, y) != black) {
          // TODO: mask?
          // why is this backwards, but the other matrixes aren't?
          leds(x, y) = text_matrix(numLEDsX - 1 - x, y);
        // } else if (g_text_complete && sprite_matrix(numLEDsX - x, y) != black) {
        //   leds(x, y) = sprite_matrix(numLEDsX - x, y);
        } else if (y < visualizerNumLEDsY) {
          leds(x, y) = visualizer_matrix(vis_x, y);
        } else {
          leds(x, y) = CRGB::Black;
        }
      }
    }

    // TODO: return false if nothing changed? we can skip drawing then
  }
#endif

void loop() {
  static bool new_frame = false;

  #ifdef INPUT_TOUCH
    if (g_touch_available) {
      // if IRQ is low, there is new touch data to read
      if (digitalRead(MPR121_IRQ) == LOW) {
        g_current_touch = cap.touched();

        g_changed_touch = g_current_touch ^ g_last_touch;

        if (setThingsFromTouch()) {
          // we can wait for a next audio frame
          // new_frame = true;
        };

        g_last_touch = g_current_touch;
      }
    } else {
      #ifdef OUTPUT_LED
        if (setBrightnessFromVolumeKnob()) {
          // this floats a little and causes draws when we don't really care
          // we can wait for a next audio frame
          // new_frame = true;
        }
      #endif
    }
  #endif

  if (fft1024.available()) {
    updateFrequencies();

    mapFrequenciesToOutputBuffer();

    new_frame = true;
  }

  #ifdef OUTPUT_LED_MATRIX
    if (g_scrolling_text != none) {
      EVERY_N_MILLIS(90) {
        // draw text
        int scrolling_ret = ScrollingMsg.UpdateText();
        // DEBUG_PRINT("Scrolling ret: ");
        // DEBUG_PRINTLN(scrolling_ret);
        if (scrolling_ret == -1) {
          // when UpdateText returns -1, there is no more text to display and text_matrix is empty
          DEBUG_PRINT("Scrolling text #");
          DEBUG_PRINT(g_scrolling_text);
          DEBUG_PRINTLN(" complete");
          g_scrolling_text = none;
        } else {
          // UpdateText drew a new frame
          new_frame = true;

          if (scrolling_ret == 1) {
            // when UpdateText returns 1, "FLASHLIGHT" text and a delay is done being displayed
            // toggle flashlight mode
            if (g_flashlight_state == on) {
              g_flashlight_state = off;

              if (g_brightness_visualizer) {
                g_brightness = g_brightness_visualizer;
              }

              // TODO: remove a spinning white light sprite in the front
            } else {
              g_flashlight_state = on;

              if (g_brightness_flashlight) {
                g_brightness = g_brightness_flashlight;
              }

              // TODO: add a spinning white light sprite in the front
            }
          }
        }
      }
    } else {
      // TODO: how often?
      EVERY_N_SECONDS(300) {
        // scroll text again
        // TODO: cycle between different text
        // TODO: instead of every_n_seconds, tie to touch sensor and to a bunch of visualizer columns hitting the top in a single frame
        setText(CHEER);

        ScrollingMsg.UpdateText();

        new_frame = true;
      }
    }

    // TODO: draw sprites if not drawing text
    // if (g_text_complete) {
    //   EVERY_N_MILLIS(1000/60) {
    //     fill_solid(sprite_matrix[0], sprite_matrix.Size(), CRGB::Black);

    //     Sprites.UpdateSprites();

    //     // TODO: do collision detection for pacman
    //     //Sprites.DetectCollisions();

    //     Sprites.RenderSprites();

    //     new_frame = true;
    //   }
    // }
  #endif

  if (new_frame) {
    #ifdef OUTPUT_LED_MATRIX
      combineMatrixes();
    #elif LIGHT_TYPE == EL_WIRE_8
      // no need to combine frames on the el wire since it only shows frequencies
    #else
      #error WIP
    #endif

    #ifdef OUTPUT_LED
      // if dithering is off, we can run at a faster framerate
      if (g_dither) {
        FastLED.delay((draw_micros * num_dither_shows) / 1000);
      } else {
        FastLED.show();
      }
    #elif LIGHT_TYPE == EL_WIRE_8
      // send the bytes to their devices
      // TODO: better support multiple devices
      Serial1.write(el_output[0]);
      //Serial2.write(el_output[1]);
      //Serial3.write(el_output[2]);

      // TODO: delay?
      // delay(9);

      // make sure we finished writing
      Serial1.flush();
      //Serial2.flush();
      //Serial3.flush();
    #endif

    // debug print
    #ifdef DEBUG_VERBOSE
      Serial.print(g_highest_ema_magnitude, 2);
      Serial.print(" / ");
      Serial.print(g_highest_max_magnitude, 2);
      Serial.print(" = ");
      Serial.print(g_highest_ema_magnitude / g_highest_max_magnitude, 2);
      Serial.print(" ");

      for (uint16_t i = 0; i < numFreqBands; i++) {
        Serial.print("| ");

        // TODO: maybe do something with parity here? 
        // do some research

        if (frequencies[i].level > 0) {
          Serial.print(frequencies[i].level);
          Serial.print(" ");
        } else {
          Serial.print("  ");
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
    #endif  // DEBUG_VERBOSE

    new_frame = false;
  }
}
