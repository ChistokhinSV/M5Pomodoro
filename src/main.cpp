/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/
#include "secrets.h"  // NOLINT

#define DEBUG 1
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <MQTTClient.h>

// #define DEBUG_NTPClient
#include <NTPClient.h>

// #include "PomodoroTimer.h"
#include <ESP32Time.h>
#include <Ticker.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "./debug.h"

#define SPEAKER_VOLUME 128
#define SCREEN_BRIGHTNESS 128

int lastrequest = 0;
bool subscribed = false;

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(8192);  // up to 8kb shadow

#include "./main.h"

// const int tz_shift = 7;  // GMT+7
const int tz_shift = 0;  // local clock to UTC

const char *ntpServer = "pool.ntp.org";
const int gmtOffset_sec = tz_shift * 3600;
timezone tz = {tz_shift * 60, DST_NONE};
const int daylightOffset_sec = 0;

ESP32Time rtc;
// ESP32Time rtc(gmtOffset_sec);

#include "./screen.h"
screenRender *active_screen;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, 60000);

void updateControls();
Ticker main_ticker(updateControls, 10);

void render_screen();
Ticker render_screen_ticker(render_screen, 250);

void connectAWS();
Ticker connect_AWS_ticker(connectAWS, 250);

void updateControls() {
  M5.update();

  auto count = M5.Touch.getCount();
  if (count) {
    sleepTicker.start();
    DEBUG_PRINTLN("sleepTicker restarted");
  }

  if (M5.BtnA.wasPressed()) {
    active_screen->setState(screenRender::ScreenState::PomodoroScreen, false,
                            true, PomodoroTimer::PomodoroLength::BIG,
                            PomodoroTimer::RestLength::REST);
  } else if (M5.BtnB.wasPressed()) {
    active_screen->setState(screenRender::ScreenState::PomodoroScreen, false,
                            true, PomodoroTimer::PomodoroLength::SMALL,
                            PomodoroTimer::RestLength::REST_SMALL);
  } else if (M5.BtnC.wasPressed()) {
    active_screen->setState(screenRender::ScreenState::PomodoroScreen, true,
                            true, PomodoroTimer::PomodoroLength::SMALL,
                            PomodoroTimer::RestLength::REST_SMALL);
  }
}

String get_topic(String thingName, bool update = false, bool accepted = false) {
  // topic name for unnamed shadow retrieve / update
  String topic;
  topic = "$aws/things/" + thingName + "/shadow/";
  topic = update ? topic + "update" : topic + "get";
  topic = accepted ? topic + "/accepted" : topic;
  DEBUG_PRINTLN("topic: " + topic);
  return topic;
}

void report_state(String timer_state, u_int32_t start_time,
                  bool reported /* = true */, bool both /* = false */) {
  char jsonBuffer[512];
  StaticJsonDocument<200> doc;

  if (reported || both) {
    doc["state"]["reported"]["timer_state"] = timer_state;
    doc["state"]["reported"]["start"] = start_time;
  }

  if (!reported || both) {
    doc["state"]["desired"]["timer_state"] = timer_state;
    doc["state"]["desired"]["start"] = start_time;
  }

  serializeJson(doc, jsonBuffer);
  client.publish(get_topic(THINGNAME, true, false), jsonBuffer);
}

void messageHandler(const String &topic, const String &payload) {
  DEBUG_PRINTLN("incoming: " + topic + " - " + payload);

  StaticJsonDocument<200> doc;
  deserializeJson(doc, payload);

  auto state = doc["state"]["desired"];
  String timer_state = state["timer_state"];
  int start_time = state["start"];

  DEBUG_PRINTLN("desired timer_state: " + timer_state + " start_time " +
                start_time);
  if (timer_state == "POMODORO") {  // TODO(ChistokhinSV) add processing for
                                    // pause and other states?
    auto current_time = rtc.getEpoch();
    auto timer_ongoing = current_time - start_time;
    if (timer_ongoing < 25 * 60) {  // small pomodoro
      active_screen->setState(screenRender::ScreenState::PomodoroScreen, false,
                              false);
      active_screen->pomodoro.adjustStart(start_time);
      // report_state(timer_state, start_time);
    } else if (timer_ongoing < 45 * 60) {  // big pomodoro
      active_screen->setState(screenRender::ScreenState::PomodoroScreen, false,
                              true, PomodoroTimer::PomodoroLength::BIG,
                              PomodoroTimer::RestLength::REST);
      active_screen->pomodoro.adjustStart(start_time);
    } else {  // stop if more than 45 minutes
      report_state("STOPPED", 0, true, true);
    }
  }
  //  const char* message = doc["message"];
}

void set_rtc() {
  m5::rtc_datetime_t datetime;
  struct tm timeinfo = rtc.getTimeStruct();
  datetime.date.year = timeinfo.tm_year + 1900;
  datetime.date.month = timeinfo.tm_mon + 1;
  datetime.date.date = timeinfo.tm_mday;
  datetime.time.hours = timeinfo.tm_hour;
  datetime.time.minutes = timeinfo.tm_min;
  datetime.time.seconds = timeinfo.tm_sec;
  M5.Rtc.setDateTime(datetime);
}

void connectAWS() {
  // DEBUG_PRINT("Connecting to Wi-Fi");
  // DEBUG_PRINTLN("connectAWS()");
  if (!client.connected()) {
    subscribed = false;
    if (WiFi.status() != WL_CONNECTED) {
      DEBUG_PRINTLN("Connecting to Wi-Fi...");
    } else {
      DEBUG_PRINTLN("Updating time...");

      timeClient.forceUpdate();

      auto ntp_epoch = timeClient.getEpochTime();
      rtc.setTime(ntp_epoch);

      // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

      struct tm timeinfo;

      if (getLocalTime(&timeinfo)) {
        rtc.setTimeStruct(timeinfo);
        set_rtc();
      } else {
        Serial.println("Failed to obtain time");
        return;
      }

      Serial.println(rtc.getTimeDate(true));

      if (!client.connected()) {
        DEBUG_PRINTLN("Connecting to AWS IOT...");
        client.connect(THINGNAME);
      }
    }
  } else {
    if (!subscribed) {
      DEBUG_PRINTLN("AWS IoT Connected!");
      client.subscribe(get_topic(THINGNAME, false, true));  // updates on GET
      // client.subscribe(get_topic(THINGNAME, true, true));   // updates on
      // UPDATE
      subscribed = true;
    }
    int curtime = rtc.getEpoch();
    if (curtime - lastrequest > 5) {
      set_rtc();
      char jsonBuffer[512];
      StaticJsonDocument<200> doc;
      doc["request"] = "GET SHADOW";
      serializeJson(doc, jsonBuffer);

      Serial.print("Publishing: ");
      Serial.println(jsonBuffer);

      client.publish(get_topic(THINGNAME, false, false),
                     jsonBuffer);  // ask for current state
      lastrequest = curtime;

      Serial.println(rtc.getTimeDate(true));
      // m5::rtc_date_t date = M5.Rtc.getDate();
    }
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

  DEBUG_PRINTLN("Wi-Fi init");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  DEBUG_PRINTLN("Speaker init");
  M5.Speaker.setVolume(SPEAKER_VOLUME);
  M5.Power.setLed(0);

  DEBUG_PRINTLN("RTC init");
  DEBUG_PRINTLN(rtc.getTimeDate(true));

  // Configure WiFiClientSecure to use the AWS IoT device credentials
  DEBUG_PRINTLN("net client init");
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  DEBUG_PRINTLN("MQTTT client init");
  client.begin(AWS_IOT_ENDPOINT, 8883, net);
  client.onMessage(messageHandler);

  DEBUG_PRINTLN("Starting timers");

  active_screen = new screenRender();

  main_ticker.start();
  render_screen_ticker.start();
  connect_AWS_ticker.start();
}

void loop() {
  main_ticker.update();
  render_screen_ticker.update();
  connect_AWS_ticker.update();

  client.loop();

  active_screen->update();
}
