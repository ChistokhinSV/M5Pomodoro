/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_wifi.h>
#include <secrets.h>

#define DEBUG 1
#include <LittleFS.h>
#include <M5Unified.h>
#include <MQTTClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "./debug.h"

#define SPEAKER_VOLUME 128
#define SCREEN_BRIGHTNESS 128

#define WAKE_TIMEOUT 30  // seconds
uint32_t lastwake;

#include <ESP32Time.h>

#include "./screen.h"

int lastrender = 0;
// screenRender active_screen;
screenRender* active_screen;

void goToSleep();
Ticker sleepTicker(goToSleep, WAKE_TIMEOUT, 1);

void updateControls();
Ticker update_controls_ticker(updateControls, 100);

void render_screen();
Ticker render_screen_ticker(render_screen, 250);

void goToSleep() {
  esp_wifi_stop();
  esp_bluedroid_disable();
  esp_bluedroid_deinit();
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
  esp_bt_mem_release(ESP_BT_MODE_BTDM);
  M5.Power.deepSleep();
}

void updateControls() {
  M5.update();
  if (M5.BtnA.wasPressed()) {
    active_screen->setState(screenRender::ScreenState::PomodoroScreen, false,
                            PomodoroTimer::PomodoroLength::BIG,
                            PomodoroTimer::RestLength::REST);
  } else if (M5.BtnB.wasPressed()) {
    active_screen->setState(screenRender::ScreenState::PomodoroScreen, false,
                            PomodoroTimer::PomodoroLength::SMALL,
                            PomodoroTimer::RestLength::REST_SMALL);
  } else if (M5.BtnC.wasPressed()) {
    active_screen->setState(screenRender::ScreenState::PomodoroScreen, true,
                            PomodoroTimer::PomodoroLength::SMALL,
                            PomodoroTimer::RestLength::REST_SMALL);
  }
}

void render_screen() { active_screen->render(); }

void initFileSystem() {
  if (!LittleFS.begin(true)) {
    DEBUG_PRINTLN("An Error has occurred while mounting LittleFS");
    return;
  } else {
    DEBUG_PRINTLN("LittleFS mounted");
  }
}

void setup() {
  DEBUG_PRINTLN("Main setup() function");
  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  M5.begin();
  DEBUG_PRINTLN("WAKEUP CAUSE: " + String(wakeup_cause));
  initFileSystem();
  M5.Lcd.setBrightness(SCREEN_BRIGHTNESS);

  M5.Speaker.setVolume(SPEAKER_VOLUME);
  M5.Power.setLed(0);

  active_screen = new screenRender();

  update_controls_ticker.start();
  render_screen_ticker.start();
}

void loop() {
  update_controls_ticker.update();
  render_screen_ticker.update();
  active_screen->update();
}
