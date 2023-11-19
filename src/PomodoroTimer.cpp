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
      pomodoroTicker([this] { this->loop(); }, 1000) {
  DEBUG_PRINTLN("PomodoroTimer initalized")
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
}

void PomodoroTimer::pauseTimer() { stopTimer(true); }

bool PomodoroTimer::isRest() { return timerState == PomodoroState::REST; }

bool PomodoroTimer::isRunning() { return timerState != PomodoroState::STOPPED; }

PomodoroTimer::PomodoroState PomodoroTimer::getState() { return timerState; }

int PomodoroTimer::getTimerPercentage() {
  int timeleft = getRemainingTime();
  int timerLen = timerState == PomodoroState::REST ? restMinutes * 60
                                                   : pomodoroMinutes * 60;
  return 100 - ((timeleft * 100) / timerLen);
}

uint32_t PomodoroTimer::getStartTime() { return pomodoroTimeStart; }

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

uint32_t PomodoroTimer::getRemainingTime() {
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

std::string PomodoroTimer::formattedTime() {
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

void PomodoroTimer::loop() {
  if (getRemainingTime() <= 0) {
    if (timerState == PomodoroState::POMODORO) {
      startRest();
    } else {
      stopTimer();
    }
  }
}

void PomodoroTimer::update() { pomodoroTicker.update(); }
