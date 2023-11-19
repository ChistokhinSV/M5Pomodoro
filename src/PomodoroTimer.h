/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/

#pragma once
#include <ESP32Time.h>
#include <Ticker.h>
#include <stdint.h>
#include <string>

#define DING_SOUND "/bell.wav"

class PomodoroTimer {
 public:
  enum class PomodoroLength { SMALL = 25, BIG = 45, MICRO = 5 };
  enum class RestLength { REST_SMALL = 5, REST = 10, REST_BIG = 20 };
  enum class PomodoroState { UNDEFINED, POMODORO, REST, PAUSED, STOPPED };

  PomodoroTimer(PomodoroLength pomodoroLength = PomodoroLength::SMALL,
                RestLength restLength = RestLength::REST_SMALL);

  void startTimer(bool reset_timer = true, bool rest = false, bool report_desired = true);
  void adjustStart(uint32_t startTime);
  void startRest();
  void stopTimer(bool pause = false);
  void pauseTimer();

  void update();

  bool isRest();
  bool isRunning();

  PomodoroState getState();
  int getTimerPercentage();
  uint32_t getStartTime();

  void setLength(PomodoroLength pomodoroLength, RestLength restLength);
  void setLength(PomodoroLength pomodoroLength);
  void setRest(RestLength restLength);

  uint32_t getRemainingTime();  // returns time in seconds
  std::string formattedTime();

  int toInt(PomodoroLength length);
  int toInt(RestLength length);

 private:
  struct WavFile {
    uint8_t* data;
    size_t size;
  };

  WavFile sound_ding;

  PomodoroState timerState;
  Ticker pomodoroTicker;

  int pomodoroMinutes;
  int restMinutes;

  uint32_t pomodoroTimeStart;
  uint32_t pomodoroTimeEnd;
  uint32_t pauseTime;

  void loop();

  void ding();
  bool loadWavFile(const char* filename, WavFile* wavFile);
};
