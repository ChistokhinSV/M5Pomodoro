/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/

#pragma once
#ifndef _MAIN_H_
#define _MAIN_H_

#include <ESP32Time.h>

extern const char *ntpServer;
extern const int gmtOffset_sec;
extern const int daylightOffset_sec;

extern ESP32Time rtc;

extern void set_rtc();
extern void report_state(String timer_state, u_int32_t start_time,
                         bool reported = true, bool both = false);

#endif  // _MAIN_H_
