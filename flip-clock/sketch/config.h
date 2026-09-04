/*
 * Shared declarations. Everything the three .ino files need to see about
 * each other lives here.
 */

#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <AccelStepper.h>

// ------------------------------------------------------------- hardware

const int PIN_HALL_HOURS   = 34;
const int PIN_HALL_MINUTES = 35;

// 34.13, not 34: the fraction is kept in float, otherwise 8 steps are lost per revolution
const float STEPS_PER_FLAP = 2048.0f / 60.0f;

const int HALL_TRIP = 2500;   // above this the magnet is under the sensor (idle ~1900, peak ~2900)
const int SPEED     = 150;

// One and a half revolutions. Anything longer means the sensor is not telling
// us about a magnet at all.
const long HOME_SCAN_LIMIT = 3072;

// After a failed homing, wait this long before winding the drum around again.
const uint32_t HOME_RETRY_MS = 5 * 60 * 1000;

// Working direction: the hours motor runs +, the minutes motor is mounted mirrored
const int DIR_HOURS   = +1;
const int DIR_MINUTES = -1;

struct Drum {
  AccelStepper &stepper;
  int           pin;
  int           dir;
  float         homeOffset;   // flaps between the magnet centre and 00
  float         target;       // accumulated target in steps
  int           value;        // number currently shown
  const char   *name;
  uint32_t      lastFail;     // millis of the last failed homing, 0 when healthy
};

extern Drum hours;
extern Drum minutes;

bool homeDrum(Drum &d);
void showNumber(Drum &d, int n);

// -------------------------------------------------------------- network

const char *const AP_NAME = "FlipClock";

// AP_PASS, UI_USER and UI_PASS live outside the repository - this one is
// public, and once a real password is committed no force push takes it back.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #error "Copy secrets.h.example to secrets.h and set your own passwords"
#endif

const uint32_t WIFI_TIMEOUT_MS = 15000;

struct Config {
  String ssid;
  String pass;
  String tz;
  bool   fmt12;
  float  offHours;
  float  offMinutes;
};

extern Config      cfg;
extern WebServer   server;
extern DNSServer   dns;
extern Preferences prefs;
extern bool        portalMode;   // running as an access point, clock is not moving

void loadConfig();
void saveConfig();
void startServer();
void startPortal();
void stopPortal();
bool connectWiFi();
