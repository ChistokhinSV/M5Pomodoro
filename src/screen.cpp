/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/

#include "screen.h"  // NOLINT

#include <LittleFS.h>
#include <M5Unified.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_wifi.h>

#include <string>

#include "./debug.h"
#define FASTLED_INTERNAL
#include <FastLED.h>

#include "./main.h"

#define WAKE_TIMEOUT 30  // seconds
void goToSleep() {
  set_rtc();
  esp_wifi_stop();
  esp_bluedroid_disable();
  esp_bluedroid_deinit();
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
  esp_bt_mem_release(ESP_BT_MODE_BTDM);
  M5.Power.deepSleep();
}
Ticker sleepTicker(goToSleep, WAKE_TIMEOUT * 1000, MILLIS);

screenRender::screenRender()
    : active_state(ScreenState::MainScreen),
      back_buffer(&M5.Lcd),
      lastrender(0) {
  sleepTicker.start();

  screen_height = M5.Lcd.height();
  screen_width = M5.Lcd.width();
  screen_center_x = screen_width / 2;
  screen_center_y = screen_height / 2;

  if (loadWavFile(DING_SOUND, &sound_ding)) {
    DEBUG_PRINTLN("WAV file loaded into memory");
  } else {
    DEBUG_PRINTLN("Failed to load WAV file");
  }

  back_buffer.setColorDepth(M5.Lcd.getColorDepth());
  back_buffer.setPsram(true);
  back_buffer.createSprite(screen_width, screen_height);
  back_buffer.setTextDatum(textdatum_t::middle_center);

  FastLED.addLeds<SK6812, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.clear();
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.show();
}

void screenRender::render() {
  switch (active_state) {
    case ScreenState::MainScreen:
      renderMainScreen();
      break;
    case ScreenState::PomodoroScreen:
    case ScreenState::PauseScreen:
      renderPomodoroScreen();
      break;
    case ScreenState::SettingsScreen:
      renderSettingsScreen();
      break;
    default:
      break;
  }
}

void screenRender::setState(ScreenState state, bool rest, bool report_desired,
                            PomodoroTimer::PomodoroLength pomodoro_minutes,
                            PomodoroTimer::RestLength pomodoro_rest_minutes) {
  if (active_state != state) {
    switch (state) {
      case ScreenState::MainScreen:
        pomodoro.stopTimer();
        sleepTicker.start();
        break;
      case ScreenState::PomodoroScreen:
        sleepTicker.stop();
        M5.update();  // clear button state
        pomodoro.setLength(pomodoro_minutes, pomodoro_rest_minutes);
        pomodoro.startTimer(true, rest, report_desired);
        break;
      default:
        break;
    }

    M5.Power.setVibration(128);
    ding();
    delay(500);
    M5.Power.setVibration(0);

    active_state = state;
  }
}

void screenRender::drawProgressBar(int x, int y, int w, int h, int val,
                                   int color) {
  back_buffer.drawRect(x, y, w, h, color);
  back_buffer.fillRect(x + 1, y + 1, w * (static_cast<float>(val) / 100.0f),
                       h - 1, color);
}

void screenRender::drawStatusIcons() {
  // at least draw a progress bar to show battery level
  int battery = M5.Power.getBatteryLevel();
  int w = 40;
  int h = 10;
  int border = 2;
  drawProgressBar(screen_width - w - border, 0, w, h + border, battery,
                  TFT_RED);
}

void screenRender::drawTaskName(String task_name, int prev_font_height) {
  back_buffer.setTextSize(0);
  back_buffer.setFont(SMALL_FONT);
  back_buffer.drawString(task_name, screen_center_x,
                         screen_center_y - prev_font_height / 2 - 20);
}

void screenRender::setCompletion(int width, CRGB color) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  int ledsToLight = 2 * ((100 - width + 19) / 20);
  int start = (NUM_LEDS - ledsToLight) / 2;

  for (int i = start; i < start + ledsToLight; i++) {
    leds[i] = color;
  }
  FastLED.show();
}

void screenRender::ding() {
  M5.Speaker.playWav(sound_ding.data, sound_ding.size);
}

bool screenRender::loadWavFile(const char* filename, WavFile* wavFile) {
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

void screenRender::pushBackBuffer() {
  drawStatusIcons();

  M5.Lcd.waitDisplay();
  back_buffer.pushSprite(&M5.Lcd, 0, 0);
}

void screenRender::update() {
  sleepTicker.update();
  pomodoro.update();

  auto pomo = pomodoro.getState();
  ScreenState newstate;
  switch (pomo) {
    case PomodoroTimer::PomodoroState::PAUSED:
      newstate = ScreenState::PauseScreen;
      break;
    case PomodoroTimer::PomodoroState::REST:
    case PomodoroTimer::PomodoroState::POMODORO:
      newstate = ScreenState::PomodoroScreen;
      break;
    case PomodoroTimer::PomodoroState::STOPPED:
      newstate = ScreenState::MainScreen;
      break;
    default:
      newstate = ScreenState::MainScreen;
      break;
  }
  if (newstate != active_state) {
    setState(newstate);
  }
}

void screenRender::renderMainScreen() {
  back_buffer.fillSprite(TFT_BLACK);
  back_buffer.drawPngFile(LittleFS, BACKGROUND);

  back_buffer.setTextColor(TEXT_COLOR);
  back_buffer.setFont(LARGE_FONT);
  back_buffer.setTextSize(2);
  int timer_text_height = back_buffer.fontHeight();

  back_buffer.drawString("TIMER", screen_center_x, screen_center_y);

  back_buffer.setTextSize(1);
  back_buffer.drawString("POMODORO", screen_center_x,
                         screen_center_y - timer_text_height / 2);

  back_buffer.setTextSize(0);
  back_buffer.setFont(SMALL_FONT);
  back_buffer.drawString("45", 55, 225);
  back_buffer.drawString("25", 160, 225);
  back_buffer.drawString("Rest", 270, 225);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  pushBackBuffer();
}

void screenRender::renderPomodoroScreen(bool pause /*= false*/) {
  back_buffer.fillSprite(TFT_BLACK);
  back_buffer.setTextColor(TIMER_COLOR);
  back_buffer.setFont(TIMER_FONT);
  back_buffer.setTextSize(2);
  back_buffer.drawString(pomodoro.formattedTime().c_str(), screen_center_x,
                         screen_center_y);

  switch (pomodoro.getState()) {
    case PomodoroTimer::PomodoroState::REST:
      drawTaskName("REST", back_buffer.fontHeight());
      break;
    case PomodoroTimer::PomodoroState::PAUSED:
      drawTaskName("PAUSED", back_buffer.fontHeight());
      break;
    default:
      break;
  }

  int progress = pomodoro.getTimerPercentage();

  drawProgressBar(0, screen_height - PROGRESS_BAR_HEIGHT, screen_width,
                  PROGRESS_BAR_HEIGHT, progress, TFT_BLUE);
  if (pomodoro.getState() == PomodoroTimer::PomodoroState::REST) {
    setCompletion(progress, CRGB::Green);
  } else {
    setCompletion(progress, CRGB::Red);
  }

  pushBackBuffer();
}

void screenRender::renderSettingsScreen() {}
