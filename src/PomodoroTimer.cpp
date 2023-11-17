/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/
#include "./debug.h"

#include "PomodoroTimer.h"

#include <ESP32Time.h>
#include <Ticker.h>
#include <stdint.h>

#include <iomanip>
#include <sstream>
#include <string>

ESP32Time rtc;

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

void PomodoroTimer::startTimer(bool reset_timer, bool rest) {
  timerState = PomodoroState::POMODORO;
  pomodoroTimeStart = rtc.getEpoch();
  if (reset_timer) {
    if (rest) {
      pomodoroTimeEnd = pomodoroTimeStart + restMinutes * 60;
      timerState = PomodoroState::REST;
    } else {
      pomodoroTimeEnd = pomodoroTimeStart + pomodoroMinutes * 60;
    }
    pauseTime = 0;
  } else {
    pomodoroTimeEnd = pomodoroTimeStart + pauseTime;
  }
  pomodoroTicker.start();
}

void PomodoroTimer::startRest() { startTimer(true, true); }

void PomodoroTimer::stopTimer(bool pause) {
  pomodoroTimeStart = 0;
  pomodoroTimeEnd = 0;
  if (pause) {
    pauseTime = rtc.getEpoch() - pomodoroTimeEnd;
    pauseTime = pauseTime > 0 ? pauseTime : 0;
  } else {
    pauseTime = 0;
  }
  timerState = pauseTime > 0 ? PomodoroState::PAUSED : PomodoroState::STOPPED;
  pomodoroTicker.stop();
}

void PomodoroTimer::pauseTimer() { stopTimer(true); }

bool PomodoroTimer::isRest() { return timerState == PomodoroState::REST; }

bool PomodoroTimer::isRunning() { return timerState != PomodoroState::STOPPED; }

PomodoroTimer::PomodoroState PomodoroTimer::getState() { return timerState; }

int PomodoroTimer::getTimerPercentage() {
  int timeleft = getRemainingTime();
  return timeleft / (pomodoroMinutes * 60) * 100;
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

void PomodoroTimer::update() {
  pomodoroTicker.update();
}
