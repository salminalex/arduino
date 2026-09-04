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
};

extern Drum hours;
extern Drum minutes;

int  readHall(int pin);
void moveFlaps(Drum &d, float flaps);
void homeDrum(Drum &d);
void gotoNumber(Drum &d, int n);
void showNumber(Drum &d, int n);

// -------------------------------------------------------------- network

// Change these before the clock leaves the house. AP_PASS protects the setup
// portal, where the WiFi password of the target network is typed in - without
// it that password would go over the air in the clear. Minimum 8 characters.
const char *const AP_NAME = "FlipClock";
const char *const AP_PASS = "flipclock";

// Guards the settings page once the clock is on a real network.
const char *const UI_USER = "admin";
const char *const UI_PASS = "flipclock";

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
bool connectWiFi();
