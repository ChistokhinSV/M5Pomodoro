/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/
#include "PomodoroTimer.h"

#include <ESP32Time.h>
#include <Ticker.h>
#include <stdint.h>

#include <iomanip>
#include <sstream>
#include <string>

#include "./debug.h"
#include "./main.h"
#include "./screen.h"
// ESP32Time rtc;

PomodoroTimer::PomodoroTimer(
    PomodoroLength pomodoroLength /* = PomodoroLength::SMALL */,
    RestLength restLength /* = RestLength::REST_SMALL */)
    : pomodoroMinutes(toInt(pomodoroLength)),
      restMinutes(toInt(restLength)),
      timerState(PomodoroState::STOPPED),
      pomodoroTimeStart(0),
      pomodoroTimeEnd(0),
      pauseTime(0),
      pomodoroTicker([this] { this->tick(); }, 1000) {
  if (loadWavFile(DING_SOUND, &sound_ding)) {
    DEBUG_PRINTLN("WAV file loaded into memory");
  } else {
    DEBUG_PRINTLN("Failed to load WAV file");
  }
  DEBUG_PRINTLN("PomodoroTimer initalized");
}

void PomodoroTimer::startTimer(bool reset_timer, bool rest,
                               bool report_desired) {
  sleepTicker.stop();
  timerState = PomodoroState::POMODORO;
  pomodoroTimeStart = rtc.getEpoch();
  String state;
  if (reset_timer) {
    if (rest) {
      pomodoroTimeEnd = pomodoroTimeStart + restMinutes * 60;
      timerState = PomodoroState::REST;
      state = "REST";
    } else {
      pomodoroTimeEnd = pomodoroTimeStart + pomodoroMinutes * 60;
      state = "POMODORO";
    }
    pauseTime = 0;
  } else {
    pomodoroTimeEnd = pomodoroTimeStart + pauseTime;
  }
  pomodoroTicker.start();
  if (report_desired) {
    report_state(state, pomodoroTimeStart, true, true);
  } else {
    report_state(state, pomodoroTimeStart);
  }
  ding();
}

void PomodoroTimer::adjustStart(uint32_t startTime) {
  if (pomodoroTimeStart != startTime) {
    pomodoroTimeStart = startTime;
    String state;
    if (timerState == PomodoroState::REST) {
      pomodoroTimeEnd = pomodoroTimeStart + restMinutes * 60;
      state = "REST";
    } else {
      pomodoroTimeEnd = pomodoroTimeStart + pomodoroMinutes * 60;
      state = "POMODORO";
    }
    report_state(state, pomodoroTimeStart, true, true);
  }
}

void PomodoroTimer::startRest() { startTimer(true, true); }

void PomodoroTimer::stopTimer(bool pause) {
  sleepTicker.start();
  pomodoroTimeStart = 0;
  pomodoroTimeEnd = 0;
  if (pause) {
    pauseTime = rtc.getEpoch() - pomodoroTimeEnd;
    pause = pauseTime > 0;
    pauseTime = pause ? pauseTime : 0;
  } else {
    pauseTime = 0;
  }
  timerState = pause ? PomodoroState::PAUSED : PomodoroState::STOPPED;
  pomodoroTicker.stop();
  report_state(pause ? "PAUSED" : "STOPPED", pomodoroTimeStart, true, true);
  ding();
}

void PomodoroTimer::pauseTimer() { stopTimer(true); }



int PomodoroTimer::getTimerPercentage() const {
  int timeleft = getRemainingTime();
  int timerLen = timerState == PomodoroState::REST ? restMinutes * 60
                                                   : pomodoroMinutes * 60;
  return 100 - ((timeleft * 100) / timerLen);
}

void PomodoroTimer::setLength(PomodoroLength pomodoroLength,
                              RestLength restLength) {
  pomodoroMinutes = toInt(pomodoroLength);
  restMinutes = toInt(restLength);
}

void PomodoroTimer::setLength(PomodoroLength pomodoroLength) {
  pomodoroMinutes = toInt(pomodoroLength);
}

void PomodoroTimer::setRest(RestLength restLength) {
  restMinutes = toInt(restLength);
}

uint32_t PomodoroTimer::getRemainingTime() const {
  switch (timerState) {
    case PomodoroState::PAUSED:
      return pauseTime;
      break;

    case PomodoroState::STOPPED:
      return 0;
      break;

    default:
      return pomodoroTimeEnd - rtc.getEpoch();
      break;
  }
}

std::string PomodoroTimer::formattedTime() const {
  uint32_t remainingTime = getRemainingTime();
  uint32_t minutes = remainingTime / 60;
  uint32_t seconds = remainingTime % 60;

  std::stringstream ss;
  ss << std::setw(2) << std::setfill('0') << minutes << ":" << std::setw(2)
     << std::setfill('0') << seconds;
  return ss.str();
}

int PomodoroTimer::toInt(PomodoroLength length) {
  return static_cast<int>(length);
}

int PomodoroTimer::toInt(RestLength length) { return static_cast<int>(length); }

void PomodoroTimer::tick() {
  if (getRemainingTime() <= 0) {
    if (timerState == PomodoroState::POMODORO) {
      startRest();
    } else {
      stopTimer();
    }
  }
}

void PomodoroTimer::ding() const {
    M5.Power.setVibration(128);
    M5.Speaker.playWav(sound_ding.data, sound_ding.size);
    delay(500);
    M5.Power.setVibration(0);
}

bool PomodoroTimer::loadWavFile(const char* filename, WavFile* wavFile) {
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

void PomodoroTimer::update() { pomodoroTicker.update(); }
