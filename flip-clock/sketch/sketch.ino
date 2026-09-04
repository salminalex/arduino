/*
 * Split-flap clock.
 *
 * hardware.ino - steppers, hall sensors, homing
 * network.ino  - WiFi, setup portal, settings page, NVS
 * config.h     - pins and tuning constants
 * secrets.h    - passwords, gitignored, copy from secrets.h.example
 *
 * No stored network, or it does not come up: the board starts an access point
 * called FlipClock and serves the settings page at 192.168.4.1 as a captive
 * portal. Once on a real network the same page lives at the board IP, or at
 * http://flipclock.local
 */

#include <ESPmDNS.h>
#include <time.h>
#include "config.h"

AccelStepper stepperHours   = AccelStepper(AccelStepper::FULL4WIRE, 13, 14, 12, 27);
AccelStepper stepperMinutes = AccelStepper(AccelStepper::FULL4WIRE, 26, 33, 25, 32);

Drum hours   = {stepperHours,   PIN_HALL_HOURS,   DIR_HOURS,   0, 0, 0, "hours",   0};
Drum minutes = {stepperMinutes, PIN_HALL_MINUTES, DIR_MINUTES, 0, 0, 0, "minutes", 0};

Config      cfg;
WebServer   server(80);
DNSServer   dns;
Preferences prefs;
bool        portalMode = false;

static bool stationReady = false;   // mDNS and SNTP set up for this link
static bool running      = false;   // NTP answered and the drums know where they are

// Runs once the station link is up, whether that happened at boot or hours
// later, when a router that was down finally came back.
static void startStation()
{
  Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());

  stopPortal();
  MDNS.begin("flipclock");
  MDNS.addService("http", "tcp", 80);

  configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.nist.gov");
  stationReady = true;
}

void setup()
{
  Serial.begin(115200);
  delay(300);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_HALL_HOURS,   ADC_11db);   // full 0..3.3V span
  analogSetPinAttenuation(PIN_HALL_MINUTES, ADC_11db);

  loadConfig();

  if (connectWiFi()) {
    startServer();
    startStation();
  } else {
    startPortal();      // keeps retrying the station link in the background
  }
}

void loop()
{
  server.handleClient();
  if (portalMode) dns.processNextRequest();

  if (!running) {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!stationReady) startStation();

    // Poll without blocking. On a network that never reaches an NTP server -
    // a captive portal at the office, say - the page has to stay responsive.
    struct tm t;
    if (!getLocalTime(&t, 0)) return;

    Serial.printf("time %02d:%02d\n", t.tm_hour, t.tm_min);
    homeDrum(hours);
    homeDrum(minutes);
    running = true;
    return;
  }

  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 2000) return;
  lastCheck = millis();

  struct tm t;
  if (!getLocalTime(&t)) return;   // never treat a failed read as midnight

  int h = t.tm_hour;
  int m = t.tm_min;

  if (cfg.fmt12) {
    h = h % 12;
    if (h == 0) h = 12;
  }

  if (h != hours.value)   showNumber(hours,   h);
  if (m != minutes.value) showNumber(minutes, m);
}
