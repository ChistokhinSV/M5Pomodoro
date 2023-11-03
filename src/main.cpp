/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/

#define DEBUG 1

#if (DEBUG == 1)
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(x, ...) Serial.printf(x, __VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(x, ...)
#endif

// #include <Arduino.h>
#define FASTLED_INTERNAL
#include <ESP32Time.h>
#include <FastLED.h>
#include <M5Unified.h>

#define SPEAKER_VOLUME 128  // 0 - 255

// SK6812 LED strip
#define LED_DATA_PIN 25
#define LED_BRIGHTNESS 20
#define NUM_LEDS 10
CRGB leds[NUM_LEDS];

#define STATE_UNDEFINED 0
#define STATE_MAIN_SCREEN 1
#define STATE_COUNTDOWN 2
#define STATE_COUNTDOWN_REST 3
int active_screen = STATE_UNDEFINED;

#define PROGRESS_BAR_HEIGHT 30

// Pomodoro settings
#define POMODORO_BIG 45
#define POMODORO_SMALL 25
#define POMODORO_MICRO 5
#define POMODORO_REST_SMALL 5
#define POMODORO_REST 10
#define POMODORO_REST_BIG 20
int pomodoro_minutes = POMODORO_SMALL;
int pomodoro_rest = POMODORO_REST;
uint32_t pomodoro_time = 0;
uint32_t pomodoro_time_end;

#define SMALL_FONT 3
#define BIG_FONT 8

ESP32Time rtc;

LGFX_Sprite back_buffer(&M5.Lcd);

void beep(bool up = true, int count = 1) {
  for (int i = 0; i < count; i++) {
    M5.Speaker.tone(up ? 1000 : 2000, 100);
    while (M5.Speaker.isPlaying()) {
      M5.delay(1);
    }
    M5.Speaker.tone(up ? 2000 : 1000, 100);
    while (M5.Speaker.isPlaying()) {
      M5.delay(1);
    }
  }
}

void draw_progress_bar(int x, int y, int w, int h, uint8_t val,
                       int color = 0x09F1, LGFX_Sprite *sprite = nullptr) {
  if (sprite == nullptr) {
    M5.Lcd.drawRect(x, y, w, h, color);
    M5.Lcd.fillRect(x + 1, y + 1, w * (static_cast<float>(val) / 100.0f), h - 1,
                    color);
  } else {
    sprite->drawRect(x, y, w, h, color);
    sprite->fillRect(x + 1, y + 1, w * (static_cast<float>(val) / 100.0f),
                     h - 1, color);
  }
}

void draw_battery(LGFX_Sprite *sprite = nullptr) {
  int battery = M5.Power.getBatteryLevel();
  draw_progress_bar(280, 0, 40, 10, battery, RED, sprite);
}

void main_screen() {
  M5.Lcd.clear();

  M5.Lcd.setRotation(1);
  M5.Lcd.setCursor(60, 90);
  M5.Lcd.setTextColor(GREEN);

  M5.Lcd.setTextSize(BIG_FONT);
  M5.Lcd.print("TIMER");

  M5.Lcd.setTextSize(SMALL_FONT);
  M5.Lcd.setCursor(40, 220);
  M5.Lcd.print("45");
  M5.Lcd.setCursor(150, 220);
  M5.Lcd.print("25");
  M5.Lcd.setCursor(270, 220);
  M5.Lcd.print("5");

  M5.Lcd.setTextSize(BIG_FONT);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  draw_battery();
  M5.Lcd.setBrightness(20);
  M5.Power.setLed(0);
}

void setup() {
  FastLED.addLeds<SK6812, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.clear();
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.show();

  M5.begin();

  back_buffer.setColorDepth(lgfx::rgb332_1Byte);
  bool created = back_buffer.createSprite(
      M5.Lcd.width(),
      M5.Lcd.height());  // Create a sprite the size of the display

  DEBUG_PRINTF("back_buffer created: %d w: %d, h: %d, c: %d\n", created, back_buffer.width(),
               back_buffer.height(), back_buffer.getColorDepth());

  M5.Speaker.setVolume(SPEAKER_VOLUME);
  beep();

  M5.Lcd.setTextSize(BIG_FONT);
}

void setCompletion(int width, CRGB color = CRGB::White) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  int ledsToLight = 2 * ((100 - width + 19) / 20);
  int start = (NUM_LEDS - ledsToLight) / 2;

  for (int i = start; i < start + ledsToLight; i++) {
    leds[i] = color;
  }
  FastLED.show();
}

void pomodoro_countdown() {
  M5.Lcd.setBrightness(200);
  M5.Power.setLed(1);

  beep();

  delay(250);   // debouncing
  M5.update();  // clear button state

  float timer_length = pomodoro_minutes * 60;
  pomodoro_time = rtc.getEpoch();
  pomodoro_time_end = pomodoro_time + pomodoro_minutes * 60;

  int timeleft = 0;
  int minutes = 0;
  int seconds = 0;

#define FORMATTED_TIME_SIZE 6
  char formattedTime[FORMATTED_TIME_SIZE];
  int lastrender = 0;

  int width = 0;

  while (rtc.getEpoch() <= pomodoro_time_end) {
    pomodoro_time = rtc.getEpoch();
    timeleft = pomodoro_time_end - pomodoro_time;

    minutes = timeleft / 60;
    seconds = timeleft % 60;
    snprintf(formattedTime, FORMATTED_TIME_SIZE, "%02d:%02d", minutes, seconds);

    if (timeleft > 0) {
      width = (1 - (timeleft / timer_length)) * 100;
    } else {
      width = 100;
    }

    if (lastrender != timeleft) {
      if (active_screen == STATE_COUNTDOWN_REST) {
        setCompletion(width, CRGB::Green);
      } else {
        setCompletion(width, CRGB::Red);
      }

      back_buffer.fillSprite(BLACK);
      draw_battery(&back_buffer);

      delay(1);

      back_buffer.setTextColor(GREEN);
      back_buffer.setTextSize(BIG_FONT);
      back_buffer.drawString(formattedTime, 60, 90);
      draw_progress_bar(0, M5.Display.height() - PROGRESS_BAR_HEIGHT,
                        M5.Display.width(), PROGRESS_BAR_HEIGHT, width, BLUE,
                        &back_buffer);

      draw_battery(&back_buffer);

      if (active_screen == STATE_COUNTDOWN_REST) {
        back_buffer.setTextSize(5);
        back_buffer.setTextColor(GREEN);
        back_buffer.setCursor(100, 45);
        back_buffer.print("REST");
        back_buffer.setTextSize(10);
      }

      M5.Display.waitDisplay();
      back_buffer.pushSprite(&M5.Display, 0, 0);

      lastrender = timeleft;
    }

    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
      active_screen = STATE_MAIN_SCREEN;
      break;
    }
    M5.update();
  }

  if (active_screen == STATE_COUNTDOWN) {
    M5.Lcd.clear();
    M5.Lcd.setCursor(60, 90);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.print("BREAK");

    M5.Power.setVibration(3);
    beep(false);
    delay(500);
    M5.Power.setVibration(0);

    active_screen = STATE_COUNTDOWN_REST;
    pomodoro_minutes = pomodoro_rest;
  } else {
    active_screen = STATE_MAIN_SCREEN;
    main_screen();
  }
}

void loop() {
  M5.update();

  if (active_screen == STATE_COUNTDOWN_REST) {
    pomodoro_countdown();
  } else if (active_screen != STATE_MAIN_SCREEN) {
    active_screen = STATE_MAIN_SCREEN;
    main_screen();
  }

  if (M5.BtnA.wasPressed()) {
    if (active_screen != STATE_COUNTDOWN) {
      active_screen = STATE_COUNTDOWN;
      pomodoro_minutes = POMODORO_BIG;
      pomodoro_rest = POMODORO_REST;
      pomodoro_countdown();
    }
  } else if (M5.BtnB.wasPressed()) {
    if (active_screen != STATE_COUNTDOWN) {
      active_screen = STATE_COUNTDOWN;
      pomodoro_minutes = POMODORO_SMALL;
      pomodoro_rest = POMODORO_REST_SMALL;
      pomodoro_countdown();
    }
  } else if (M5.BtnC.wasPressed()) {
    if (active_screen != STATE_COUNTDOWN) {
      active_screen = STATE_COUNTDOWN;
      pomodoro_minutes = POMODORO_MICRO;
      pomodoro_rest = POMODORO_REST_SMALL;
      pomodoro_countdown();
    }
  }
}
