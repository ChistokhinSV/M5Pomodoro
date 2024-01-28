/**

M5Stack pomodoro timer
Copyright 2023 Sergei Chistokhin

**/
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <MQTTClient.h>

#include "secrets.h"  // NOLINT

#define DEBUG_NTPClient
#include <NTPClient.h>

// #include "PomodoroTimer.h"
#include <ESP32Time.h>
#include <Ticker.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "./debug.h"
#include "./main.h"

#include "MODULE_HMI.h"
MODULE_HMI hmi;

// 50%
// #define SPEAKER_VOLUME 128
// 100%
#define SPEAKER_VOLUME 255

#define SCREEN_BRIGHTNESS 128

#define NTP_UPDATE 60  // resync every 15 seconds
uint32_t lastrequest = 0;
uint32_t lastTimeUpdate = 0;
bool subscribed = false;

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(8192);  // up to 8kb shadow

// const int tz_shift = 7;  // GMT+7
const int tz_shift = 0;  // local clock to UTC

const char *ntpServer = "pool.ntp.org";
const int gmtOffset_sec = tz_shift * 3600;
timezone tz = {tz_shift * 60, DST_NONE};
const int daylightOffset_sec = 0;

ESP32Time rtc;
// ESP32Time rtc(gmtOffset_sec);


#include "./screen.h"
#include "Arduino.h"
screenRender *active_screen;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, 60000);

void updateControls();
Ticker main_ticker(updateControls, 10);

void render_screen();
Ticker render_screen_ticker(render_screen, 250);

void hmi_read();
int32_t inc_count      = 0;
Ticker hmi_read_ticker(hmi_read, 100);

// void connectAWS();
// Ticker connect_AWS_ticker(connectAWS, 250);

// Global flag to indicate a Wi-Fi reconnect is needed
volatile bool wifiReconnectNeeded = false;
// volatile bool mqttReconnectNeeded = false;
volatile bool net_init = false;

struct WavFile {
  uint8_t *data;
  size_t size;
};

WavFile sound_ding;

struct reportState {
  bool sent = true;
  bool reported = true;
  bool both = false;
  String timer_state;
  u_int32_t start_time;
} reportstate;

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
  DEBUG_PRINTLN("report_state");
  reportstate.reported = reported;
  reportstate.both = both;
  reportstate.timer_state = timer_state;
  reportstate.start_time = start_time;
  reportstate.sent = false;
}

void send_report_state() {
  if (!reportstate.sent && client.connected()) {
    char jsonBuffer[512];
    StaticJsonDocument<200> doc;

    if (reportstate.reported || reportstate.both) {
      doc["state"]["reported"]["timer_state"] = reportstate.timer_state;
      doc["state"]["reported"]["start"] = reportstate.start_time;
      doc["state"]["reported"]["description"] = active_screen->getTaskName();
    }

    if (!reportstate.reported || reportstate.both) {
      doc["state"]["desired"]["timer_state"] = reportstate.timer_state;
      doc["state"]["desired"]["start"] = reportstate.start_time;
    }

    serializeJson(doc, jsonBuffer);
    DEBUG_PRINTLN("send_report_state reported: " + String(jsonBuffer));
    auto published =
        client.publish(get_topic(THINGNAME, true, false), jsonBuffer);
    if (published) {
      reportstate.sent = true;
    }
  }
}

void updateControls() {
  M5.update();

  auto count = M5.Touch.getCount();
  if (count) {
    if (sleepTicker.state() == RUNNING) {
      sleepTicker.start();
      DEBUG_PRINTLN("sleepTicker restarted");
    }
  }

  switch (active_screen->getState()) {
    case screenRender::ScreenState::MainScreen:
      if (M5.BtnA.wasPressed()) {
        active_screen->setState(screenRender::ScreenState::PomodoroScreen,
                                false, true, PomodoroTimer::PomodoroLength::BIG,
                                PomodoroTimer::RestLength::REST);
      } else if (M5.BtnB.wasPressed()) {
        active_screen->setState(screenRender::ScreenState::PomodoroScreen,
                                false, true,
                                PomodoroTimer::PomodoroLength::SMALL,
                                PomodoroTimer::RestLength::REST_SMALL);
      } else if (M5.BtnC.wasPressed()) {
        active_screen->setState(screenRender::ScreenState::PomodoroScreen, true,
                                true, PomodoroTimer::PomodoroLength::SMALL,
                                PomodoroTimer::RestLength::REST_SMALL);
      }
      break;
    case screenRender::ScreenState::PomodoroScreen:
      // break; // or any other for now - to the main screen
    default:
      if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() ||
          M5.BtnC.wasPressed()) {
        active_screen->setState(screenRender::ScreenState::MainScreen);
      }
      break;
  }
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
  if (reportstate
          .sent) {  // only act on recieve if there is nothing to send (?)
    if (timer_state == "POMODORO") {  // TODO(ChistokhinSV) add processing for
                                      // pause and other states?
      auto current_time = rtc.getEpoch();
      auto timer_ongoing = current_time - start_time;

      auto description = state["description"];
      if (description) {
        active_screen->setTaskName(description);

        if (active_screen->pomodoro.getState() !=
                PomodoroTimer::PomodoroState::POMODORO ||
            active_screen->pomodoro.getStartTime() != start_time) {
          if (timer_ongoing < 25 * 60) {  // small pomodoro
            active_screen->setState(screenRender::ScreenState::PomodoroScreen,
                                    false, false);
            active_screen->pomodoro.adjustStart(start_time);
            // report_state(timer_state, start_time);
          } else if (timer_ongoing < 45 * 60) {  // big pomodoro
            active_screen->setState(screenRender::ScreenState::PomodoroScreen,
                                    false, false,
                                    PomodoroTimer::PomodoroLength::BIG,
                                    PomodoroTimer::RestLength::REST);
            active_screen->pomodoro.adjustStart(start_time);
          } else {  // stop if more than 45 minutes
            active_screen->setState(screenRender::ScreenState::MainScreen);
            report_state("STOPPED", 0, true, true);
          }
        }
      }

    } else if (timer_state == "STOPPED") {
      if (active_screen->pomodoro.getState() !=
          PomodoroTimer::PomodoroState::STOPPED) {
        active_screen->setState(screenRender::ScreenState::MainScreen);
        report_state("STOPPED", 0, true, false);
      }
    } else if (timer_state == "REST") {
      if (active_screen->pomodoro.getState() !=
          PomodoroTimer::PomodoroState::REST) {
        DEBUG_PRINTLN("REST by MQTT");
        active_screen->setState(screenRender::ScreenState::PomodoroScreen, true,
                                false, PomodoroTimer::PomodoroLength::SMALL,
                                PomodoroTimer::RestLength::REST_SMALL);
        DEBUG_PRINTLN("adjustStart REST by MQTT");
        active_screen->pomodoro.adjustStart(start_time);
        DEBUG_PRINTLN("REST by MQTT complete");
      }
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
  DEBUG_PRINTLN("set_rtc(): " + rtc.getTimeDate(true));
}

void render_screen() { active_screen->render(); }

void hmi_read() {
  // DEBUG_PRINTLN("hmi_read()");
  // DEBUG_PRINTF("Encoder value: %d\n", hmi.getEncoderValue());
  inc_count = hmi.getIncrementValue();
  // DEBUG_PRINTF("Encoder inc value: %d\n", inc_count);
  // DEBUG_PRINTF("btnS:%d, btnA:%d, btnB:%d\n", hmi.getButtonS(),
  //                 hmi.getButton1(), hmi.getButton2());
  int step = inc_count / 4;
  if (step) {
    DEBUG_PRINTLN("step: " + String(step));
  }
}

void initFileSystem() {
  if (!LittleFS.begin(true)) {
    DEBUG_PRINTLN("An Error has occurred while mounting LittleFS");
    return;
  } else {
    DEBUG_PRINTLN("LittleFS mounted");
  }
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_STA_DISCONNECTED:
      DEBUG_PRINTLN("Wi-Fi disconnected, setting flag for reconnect...");
      wifiReconnectNeeded = true;
      break;
    case SYSTEM_EVENT_STA_LOST_IP:
      DEBUG_PRINTLN("Wi-Fi lost IP, setting flag for reconnect...");
      wifiReconnectNeeded = true;
      break;
    case SYSTEM_EVENT_WIFI_READY:
      DEBUG_PRINTLN("Wi-Fi Ready");
      break;
    default:
      break;
  }
}

void networkTask(void *pvParameters) {
  DEBUG_PRINTLN("networkTask()");

  for (;;) {
    auto ntp_epoch = timeClient.isTimeSet() ? timeClient.getEpochTime() : 0;

    if (wifiReconnectNeeded) {
      DEBUG_PRINTLN("Wi-Fi init begins");
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      wifiReconnectNeeded = false;
    }
    auto wifi_connected = WiFi.isConnected();

    if (wifi_connected) {
      timeClient.update();
    }

    if (wifi_connected && (ntp_epoch - lastTimeUpdate) > NTP_UPDATE) {
      DEBUG_PRINTLN("Updating time...");
      lastTimeUpdate = ntp_epoch;
      auto time_updated = timeClient.isTimeSet();

      auto currentRTC = rtc.getEpoch();
      auto currentNTP = time_updated ? timeClient.getEpochTime() : 0;
      auto diff = currentNTP - currentRTC;
      if (timeClient.isTimeSet() && currentRTC > 0 && currentNTP > 0 &&
          diff > 1) {  // if there is more than 1 second difference
        DEBUG_PRINTLN("RTC update, currentRTC=" + String(currentRTC) +
                      ", currentNTP=" + String(currentNTP) +
                      ", diff=" + String(diff));
        DEBUG_PRINTLN("networkTask rtc.getTimeDate(): " +
                      rtc.getTimeDate(true));
        rtc.setTime(ntp_epoch);
        active_screen->pomodoro.shift(diff);
        set_rtc();

        DEBUG_PRINTLN("RTC updated, shifted " + String(diff) + " seconds");
      }
    }

    if (!net_init) {
      // Configure WiFiClientSecure to use the AWS IoT device credentials
      netClientInit();
    }

    if (wifi_connected && !client.connected()) {
      DEBUG_PRINTLN("Connecting to AWS IOT...");
      client.connect(THINGNAME);
      subscribed = false;
      lastrequest = 0;
    }

    if (wifi_connected && client.connected()) {
      if (!subscribed) {
        DEBUG_PRINTLN("AWS IoT Connected!");
        client.subscribe(get_topic(THINGNAME, false, true));  // updates on GET
        client.subscribe(
            get_topic(THINGNAME, true, true));  // updates on UPDATE
        subscribed = true;
        timeClient.begin();
        timeClient.update();
      }

      if (lastrequest == 0 && reportstate.sent) {
        getDeviceShadow();
      }

      send_report_state();
    }

    // Other network task activities
    if (wifi_connected && client.connected()) {
      client.loop();
    }

    if (wifi_connected) {
      timeClient.update();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void netClientInit() {
  DEBUG_PRINTLN("net client init");
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);
  net.setTimeout(10);
  net.setHandshakeTimeout(15);

  client.begin(AWS_IOT_ENDPOINT, 8883, net);
  client.onMessage(messageHandler);

  net_init = true;
}

void getDeviceShadow() {
  DEBUG_PRINTLN("Getting the device shadow...");

  char jsonBuffer[512];
  StaticJsonDocument<200> doc;
  doc["request"] = "GET SHADOW";
  serializeJson(doc, jsonBuffer);

  Serial.print("Publishing: ");
  Serial.println(jsonBuffer);

  client.publish(get_topic(THINGNAME, false, false),
                 jsonBuffer);  // ask for current state
  lastrequest = rtc.getEpoch();

  Serial.println(rtc.getTimeDate(true));
}
void setup() {
  DEBUG_PRINTLN("Main setup() function");
  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  M5.begin();

  // timeClient.end();  // stop NTP client to reduce affecting SSL handshake

  DEBUG_PRINTLN("WAKEUP CAUSE: " + String(wakeup_cause));

  WiFi.onEvent(WiFiEvent);
  wifiReconnectNeeded = true;
  xTaskCreatePinnedToCore(networkTask,   /* Function to implement the task */
                          "NetworkTask", /* Name of the task */
                          10000,         /* Stack size in words */
                          NULL,          /* Task input parameter */
                          1,             /* Priority of the task */
                          NULL,          /* Task handle */
                          1);            /* Core where the task should run */

  initFileSystem();
  M5.Lcd.setBrightness(SCREEN_BRIGHTNESS);

  DEBUG_PRINTLN("Speaker init");
  M5.Speaker.setVolume(SPEAKER_VOLUME);
  M5.Power.setLed(0);

  DEBUG_PRINTLN("RTC init");
  DEBUG_PRINTLN(rtc.getTimeDate(true));

  DEBUG_PRINTLN("HMI init");
  hmi.begin(&Wire1, HMI_ADDR, 21, 22, 100000);
  DEBUG_PRINTLN(hmi.getFirmwareVersion());

  DEBUG_PRINTLN("Starting timers");

  active_screen = new screenRender();

  main_ticker.start();
  render_screen_ticker.start();
  // connect_AWS_ticker.start();
  hmi_read_ticker.start();
}

void loop() {
  main_ticker.update();
  render_screen_ticker.update();
  hmi_read_ticker.update();
  // connect_AWS_ticker.update();

  active_screen->update();
}
