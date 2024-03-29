/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/

#include "screen.h"  // NOLINT

#include <LittleFS.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_wifi.h>

#include <string>

#include "./debug.h"
#include "./main.h"

#define FASTLED_INTERNAL
#include <FastLED.h>

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
      description(""),
      transition(false),
      lastrender(0) {
  sleepTicker.start();

  screen_height = M5.Lcd.height();
  screen_width = M5.Lcd.width();
  screen_center_x = screen_width / 2;
  screen_center_y = screen_height / 2;

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
                            int pomodoro_minutes,
                            PomodoroTimer::RestLength pomodoro_rest_minutes) {
  DEBUG_PRINTLN("screenRender::setState");
  transition = true;
  if (active_state != state) {
    switch (state) {
      case ScreenState::MainScreen:
        DEBUG_PRINTLN("screenRender::setState to MainScreen");
        if (pomodoro.isRunning()) {
          pomodoro.stopTimer();
        }
        description = "";
        break;
      case ScreenState::PomodoroScreen:
        DEBUG_PRINTLN("screenRender::setState to PomodoroScreen");
        M5.update();  // clear button state
        pomodoro.setLength(pomodoro_minutes, pomodoro_rest_minutes);
        pomodoro.startTimer(true, rest, report_desired);
        break;
      default:
        DEBUG_PRINTLN("screenRender::setState to (other_state?)");
        break;
    }
    active_state = state;
  }
  transition = false;
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
  int power_drain = M5.Power.getBatteryCurrent();

  String power_drain_str = String(power_drain) + " mA";
  if (M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging) {
    power_drain_str += " (+)";
  }

  int w = 40;
  int h = 10;
  int border = 2;
  auto icon_x = screen_width - w - border;
  back_buffer.setTextSize(0);
  drawProgressBar(icon_x, border, w, h + border, battery, TFT_RED);
  back_buffer.setTextColor(TFT_WHITE);
  back_buffer.drawString(String(battery), icon_x + w / 2, h / 2 + border,
                         &Font8x8C64);

  back_buffer.setTextDatum(textdatum_t::top_left);
  back_buffer.drawString(power_drain_str, border, h / 2 + border, &Font8x8C64);
  back_buffer.setTextDatum(textdatum_t::middle_center);

  if (WiFi.status() == WL_CONNECTED) {
    back_buffer.drawPngFile(LittleFS, ICON_WIFI, icon_x - border - h, border);
  } else {
    back_buffer.drawPngFile(LittleFS, ICON_NOWIFI, icon_x - border - h, border);
  }
  if (client.connected()) {
    back_buffer.drawPngFile(LittleFS, ICON_MQTT, icon_x - (border + h) * 2,
                            border);
  }
}

void screenRender::drawTaskName(String task_name, int prev_font_height) {
  if (task_name.length() > 0) {
    back_buffer.setTextSize(0);
    back_buffer.setFont(SMALL_FONT);
    back_buffer.drawString(task_name, screen_center_x,
                           screen_center_y - prev_font_height / 2 - 30);
  }
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

void screenRender::pushBackBuffer() {
  drawStatusIcons();

  M5.Lcd.waitDisplay();
  back_buffer.pushSprite(&M5.Lcd, 0, 0);
}

void screenRender::update() {
  sleepTicker.update();
  pomodoro.update();

  if (!transition) {
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
  back_buffer.drawString(String(pomodoro_minutes_cfg), 55, 225);
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
    case PomodoroTimer::PomodoroState::POMODORO:
      drawTaskName(description, back_buffer.fontHeight());
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
