/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/
#include <secrets.h>

#define DEBUG 1
#include <LittleFS.h>
#include <M5Unified.h>
#include <MQTTClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Ticker.h>

#include "./debug.h"

#define SPEAKER_VOLUME 128
#define SCREEN_BRIGHTNESS 128

#include <ESP32Time.h>

#include "./screen.h"

int lastrender = 0;
// screenRender active_screen;
screenRender* active_screen;

void updateControls();
Ticker main_ticker(updateControls, 100);

void render_screen();
Ticker render_screen_ticker(render_screen, 250);

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

  main_ticker.start();
  render_screen_ticker.start();
}

void loop() {
  main_ticker.update();
  render_screen_ticker.update();
  active_screen->update();
}
