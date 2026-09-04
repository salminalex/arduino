/*
 * Split-flap clock.
 *
 * hardware.ino - steppers, hall sensors, homing
 * network.ino  - WiFi, setup portal, settings page, NVS
 * config.h     - pins, tuning constants, passwords
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

Drum hours   = {stepperHours,   PIN_HALL_HOURS,   DIR_HOURS,   0, 0, 0, "hours"};
Drum minutes = {stepperMinutes, PIN_HALL_MINUTES, DIR_MINUTES, 0, 0, 0, "minutes"};

Config      cfg;
WebServer   server(80);
DNSServer   dns;
Preferences prefs;
bool        portalMode = false;

void setup()
{
  Serial.begin(115200);
  delay(300);
  analogReadResolution(12);

  loadConfig();

  if (!connectWiFi()) {
    startPortal();
    return;
  }

  Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  MDNS.begin("flipclock");
  startServer();

  configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.nist.gov");

  struct tm t;
  Serial.print("NTP");
  while (!getLocalTime(&t)) { delay(500); Serial.print('.'); }
  Serial.printf("\ntime %02d:%02d\n", t.tm_hour, t.tm_min);

  homeDrum(hours);
  homeDrum(minutes);
}

void loop()
{
  server.handleClient();

  if (portalMode) {
    dns.processNextRequest();
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
