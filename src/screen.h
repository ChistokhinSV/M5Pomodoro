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
#define DING_SOUND "/bell.wav"

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

class screenRender {
 public:
  PomodoroTimer pomodoro;

  enum class ScreenState {
    Undefined,
    MainScreen,
    PomodoroScreen,
    SettingsScreen,
    PauseScreen
  };

  screenRender();
  void render();
  void update();
  void setState(
      ScreenState state,
      bool rest = false,
      PomodoroTimer::PomodoroLength pomodoro_minutes = PomodoroTimer::PomodoroLength::SMALL,
      PomodoroTimer::RestLength pomodoro_rest_minutes = PomodoroTimer::RestLength::REST_SMALL);

 private:
  struct WavFile {
    uint8_t* data;
    size_t size;
  };

  WavFile sound_ding;

  ScreenState active_state;
  M5Canvas back_buffer;
  int lastrender;

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
  void ding();
  bool loadWavFile(const char* filename, WavFile* wavFile);
  void pushBackBuffer();
};
