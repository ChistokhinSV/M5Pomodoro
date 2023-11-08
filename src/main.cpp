/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/

#include <LittleFS.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_wifi.h>
#include <secrets.h>
#include <WiFi.h>
#include <mqtt_client.h>

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

#define FASTLED_INTERNAL
#include <ESP32Time.h>
#include <FastLED.h>
#include <M5Unified.h>

#define SPEAKER_VOLUME 128

// SK6812 LED strip
#define LED_DATA_PIN 25
#define LED_BRIGHTNESS 20
#define NUM_LEDS 10
CRGB leds[NUM_LEDS];

#define SCREEN_BRIGHTNESS 128

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

#define TEXT_COLOR WHITE
#define TIMER_COLOR GREEN

#define WAKE_TIMEOUT 30
uint32_t lastwake;

ESP32Time rtc;

#define SMALL_FONT &fonts::Orbitron_Light_24
#define SMALL_FONT_SIZE 2
#define LARGE_FONT &fonts::Orbitron_Light_32
#define LARGE_FONT_SIZE 1
#define LARGE_FONT_SIZE_BIG 2
#define CLOCK_FONT &fonts::Font7

// Structure to hold WAV file data
struct WavFile {
  uint8_t* data;
  size_t size;
};

#define BACKGROUND "/background1.png"

WavFile sound_ding;

LGFX_Sprite back_buffer(&M5.Lcd);

int screen_size_x;
int screen_center_x;
int screen_size_y;
int screen_center_y;

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

void ding() { M5.Speaker.playWav(sound_ding.data, sound_ding.size); }

// Function to load a WAV file into memory
bool loadWavFile(const char* filename, WavFile* wavFile) {
  if (wavFile == nullptr) {
    return false;  // Pointer is null, cannot proceed
  }

  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.printf("Failed to open % for reading", filename);
    return false;
  }

  wavFile->size = file.size();
  wavFile->data = new uint8_t[wavFile->size];
  if (file.read(wavFile->data, wavFile->size) != wavFile->size) {
    Serial.println("Failed to read file into memory");
    delete[] wavFile->data;
    return false;
  }

  file.close();
  return true;
}

// Function to initialize the file system
void initFileSystem() {
  if (!LittleFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
}

void draw_progress_bar(int x, int y, int w, int h, uint8_t val,
                       int color = 0x09F1, LGFX_Sprite* sprite = nullptr) {
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

void draw_task_name(String task_name, LGFX_Sprite* sprite, int prev_font_height = 0) {
  sprite->setTextSize(0);
  sprite->setFont(SMALL_FONT);
  sprite->drawString(task_name, screen_center_x, screen_center_y - prev_font_height/2, SMALL_FONT);
}

void draw_battery(LGFX_Sprite* sprite = nullptr) {
  int battery = M5.Power.getBatteryLevel();
  int w = 40;
  int h = 10;
  int border = 2;
  draw_progress_bar(screen_size_x - w - border, 0, w, h + border, battery, RED,
                    sprite);
}

void main_screen() {
  lastwake = rtc.getEpoch();
  ding();

  M5.Lcd.clear();
  M5.Lcd.drawPngFile(LittleFS, BACKGROUND);

  M5.Lcd.setFont(LARGE_FONT);
  M5.Lcd.setTextSize(2);
  int timer_text_height = M5.Lcd.fontHeight(LARGE_FONT);
  M5.Lcd.drawString("TIMER", screen_center_x, screen_center_y, LARGE_FONT);

  M5.Lcd.setTextSize(1);
  M5.Lcd.drawString("POMODORO", screen_center_x,
                    screen_center_y - timer_text_height / 2, LARGE_FONT);

  M5.Lcd.setTextSize(0);
  M5.Lcd.setFont(SMALL_FONT);
  M5.Lcd.drawString("45", 55, 225);
  M5.Lcd.drawString("25", 160, 225);
  M5.Lcd.drawString("Rest", 270, 225);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  draw_battery();
  M5.Lcd.setBrightness(SCREEN_BRIGHTNESS);
  M5.Power.setLed(0);
}

void setup() {
  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  M5.begin();
  DEBUG_PRINTLN("WAKEUP CAUSE: " + String(wakeup_cause));

  initFileSystem();

  screen_size_x = M5.Lcd.width();
  screen_center_x = screen_size_x / 2;
  screen_size_y = M5.Lcd.height();
  screen_center_y = screen_size_y / 2;

  // Load WAV files
  if (loadWavFile("/bell.wav", &sound_ding)) {
    Serial.println("WAV file loaded into memory");
  } else {
    Serial.println("Failed to load WAV file");
  }

    // M5.Display.println("Connecting to WiFi");

    WiFi.disconnect();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);

    #if defined ( WIFI_SSID ) &&  defined ( WIFI_PASS )
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    #else
      WiFi.begin();
    #endif

    // Serial.print("Connecting WiFi");
    // while (WiFi.status() != WL_CONNECTED) {
    //   Serial.print(".");
    //   delay(100);
    // }
    // Serial.println();
    // DEBUG_PRINTLN("Connected to WiFi");
    // DEBUG_PRINTLN("IP: " + WiFi.localIP().toString());

  M5.Lcd.setTextDatum(textdatum_t::middle_center);

  FastLED.addLeds<SK6812, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.clear();
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.show();

  back_buffer.setColorDepth(M5.Lcd.getColorDepth());
  back_buffer.setPsram(true);
  bool created = back_buffer.createSprite(screen_size_x, screen_size_y);
  back_buffer.setTextDatum(textdatum_t::middle_center);

  M5.Speaker.setVolume(SPEAKER_VOLUME);

  M5.Lcd.setTextColor(TEXT_COLOR);
  M5.Lcd.setRotation(1);

  lastwake = rtc.getEpoch();
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
  // M5.Lcd.setBrightness(SCREEN_BRIGHTNESS);
  M5.Power.setLed(1);

  ding();

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

  int progress = 0;

  while (rtc.getEpoch() <= pomodoro_time_end) {
    pomodoro_time = rtc.getEpoch();
    timeleft = pomodoro_time_end - pomodoro_time;

    minutes = timeleft / 60;
    seconds = timeleft % 60;
    snprintf(formattedTime, FORMATTED_TIME_SIZE, "%02d:%02d", minutes, seconds);

    if (timeleft > 0) {
      progress = (1 - (timeleft / timer_length)) * 100;
    } else {
      progress = 100;
    }

    if (lastrender != timeleft) {
      if (active_screen == STATE_COUNTDOWN_REST) {
        setCompletion(progress, CRGB::Green);
      } else {
        setCompletion(progress, CRGB::Red);
      }

      back_buffer.fillSprite(BLACK);
      // back_buffer.drawPngFile(LittleFS, BACKGROUND);
      draw_battery(&back_buffer);

      back_buffer.setTextColor(TIMER_COLOR);
      back_buffer.setTextSize(2);
      back_buffer.setFont(CLOCK_FONT);
      back_buffer.drawString(formattedTime, screen_center_x, screen_size_y * 0.6, CLOCK_FONT);

      int prev_font_height = back_buffer.fontHeight();

      if (active_screen == STATE_COUNTDOWN_REST) {
        draw_task_name("REST", &back_buffer, prev_font_height);
      } else {
        draw_task_name("Pomodoro timer", &back_buffer, prev_font_height);
      }

      draw_progress_bar(0, M5.Lcd.height() - PROGRESS_BAR_HEIGHT,
                        M5.Lcd.width(), PROGRESS_BAR_HEIGHT, progress, BLUE,
                        &back_buffer);

      draw_battery(&back_buffer);

      M5.Lcd.waitDisplay();
      back_buffer.pushSprite(&M5.Lcd, 0, 0);

      lastrender = timeleft;
    }

    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
      active_screen = STATE_MAIN_SCREEN;
      break;
    }
    M5.update();
    delay(1);
  }

  if (active_screen == STATE_COUNTDOWN) {
    M5.Lcd.clear();

    M5.Lcd.setTextSize(LARGE_FONT_SIZE_BIG);
    M5.Lcd.setFont(LARGE_FONT);
    const String break_text = "BREAK";
    int break_size = M5.Lcd.textWidth(break_text, LARGE_FONT);
    M5.Lcd.drawString(break_text, M5.Lcd.width() / 2 - break_size / 2, 90,
                      LARGE_FONT);

    M5.Power.setVibration(128);
    ding();
    delay(500);
    M5.Power.setVibration(0);

    active_screen = STATE_COUNTDOWN_REST;
    pomodoro_minutes = pomodoro_rest;
    lastwake = rtc.getEpoch();
  } else {
    M5.Power.setVibration(128);
    ding();
    delay(500);
    M5.Power.setVibration(0);

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

  if (M5.BtnA.wasPressed()) {  // big tomato
    if (active_screen != STATE_COUNTDOWN) {
      active_screen = STATE_COUNTDOWN;
      pomodoro_minutes = POMODORO_BIG;
      pomodoro_rest = POMODORO_REST;
      pomodoro_countdown();
    }
  } else if (M5.BtnB.wasPressed()) {  // small tomato
    if (active_screen != STATE_COUNTDOWN) {
      active_screen = STATE_COUNTDOWN;
      pomodoro_minutes = POMODORO_SMALL;
      pomodoro_rest = POMODORO_REST_SMALL;
      pomodoro_countdown();
    }
  } else if (M5.BtnC.wasPressed()) {  // Rest
    if (active_screen != STATE_COUNTDOWN_REST) {
      active_screen = STATE_COUNTDOWN_REST;
      pomodoro_minutes = POMODORO_REST;
      pomodoro_rest = POMODORO_REST;
      pomodoro_countdown();
    }
  }

  if (rtc.getEpoch() - lastwake > WAKE_TIMEOUT) {
    esp_wifi_stop();
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    esp_bt_mem_release(ESP_BT_MODE_BTDM);

    M5.Power.deepSleep();
  }

  delay(10);
}
