/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/

#pragma once
#define FASTLED_INTERNAL
#include <FastLED.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <PomodoroTimer.h>

#define BACKGROUND "/background1.png"
#define ICON_MQTT "/MQTT.png"
#define ICON_WIFI "/WIFI.png"
#define ICON_NOWIFI "/NOWIFI.png"

#define SMALL_FONT &fonts::Orbitron_Light_24
#define LARGE_FONT &fonts::Orbitron_Light_32
#define TIMER_FONT &fonts::Font7

#define TEXT_COLOR TFT_WHITE
#define TIMER_COLOR TFT_GREEN

// SK6812 LED strip
#define LED_DATA_PIN 25
#define LED_BRIGHTNESS 20
#define NUM_LEDS 10

#define PROGRESS_BAR_HEIGHT 40

extern Ticker sleepTicker;

class screenRender {
 public:
  PomodoroTimer pomodoro;
  int pomodoro_minutes_cfg = static_cast<int>(PomodoroTimer::PomodoroLength::BIG);

  enum class ScreenState {
    Undefined,
    MainScreen,
    PomodoroScreen,
    SettingsScreen,
    PauseScreen
  };

  screenRender();
  void render();
  void setState(ScreenState state, bool rest = false,
                bool report_desired = true,
                int pomodoro_minutes = static_cast<int>(PomodoroTimer::PomodoroLength::SMALL),
                PomodoroTimer::RestLength pomodoro_rest_minutes =
                    PomodoroTimer::RestLength::REST_SMALL);
  ScreenState getState() const { return active_state; }
  void update();
  void setTaskName(String taskName) { description = taskName; }
  String getTaskName() const { return description; }

 private:
  ScreenState active_state;
  M5Canvas back_buffer;
  int lastrender;
  String description;
  bool transition;  // in transition state, no need to check it

  int screen_width;
  int screen_height;
  int screen_center_x;
  int screen_center_y;

  PomodoroTimer::PomodoroState last_pomodoro_state =
      PomodoroTimer::PomodoroState::UNDEFINED;

  CRGB leds[NUM_LEDS];

  void renderMainScreen();
  void renderPomodoroScreen(bool pause = false);
  void renderSettingsScreen();

  void drawProgressBar(int x, int y, int w, int h, int val,
                       int color = TFT_BLUE);
  void drawStatusIcons();
  void drawTaskName(String task_name, int prev_font_height = 0);
  void setCompletion(int width, CRGB color = CRGB::White);
  void pushBackBuffer();
};
