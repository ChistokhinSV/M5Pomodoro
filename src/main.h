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

#endif  // _MAIN_H_
