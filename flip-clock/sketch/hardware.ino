/*
 * Steppers and hall sensors: homing, positioning, nothing else.
 */

#include "config.h"

int readHall(int pin)
{
  uint32_t acc = 0;
  for (int i = 0; i < 4; i++) acc += analogRead(pin);   // the ESP32 ADC is noisy
  return acc / 4;
}

void moveFlaps(Drum &d, float flaps)
{
  d.target += d.dir * flaps * STEPS_PER_FLAP;
  d.stepper.setMaxSpeed(SPEED);
  d.stepper.setAcceleration(SPEED);
  d.stepper.moveTo(lround(d.target));

  while (d.stepper.distanceToGo() != 0) d.stepper.run();
  d.stepper.disableOutputs();   // drop the coil current, the motor would cook otherwise
}

static bool homeFailed(Drum &d, const char *why)
{
  d.stepper.disableOutputs();
  d.lastFail = millis();
  Serial.printf("[!] %s: %s, ADC=%d\n", d.name, why, readHall(d.pin));
  return false;
}

bool homeDrum(Drum &d)
{
  // A drum that just failed is not worth winding around again every couple of
  // seconds - the sensor is not going to have healed in the meantime.
  if (d.lastFail && millis() - d.lastFail < HOME_RETRY_MS) return false;

  d.stepper.setMaxSpeed(SPEED);
  d.stepper.setSpeed(d.dir * SPEED);

  long guard = 0;

  // already sitting on the magnet - step off it first
  while (readHall(d.pin) >= HALL_TRIP) {
    if (d.stepper.runSpeed()) guard++;
    if (guard > HOME_SCAN_LIMIT) return homeFailed(d, "sensor stuck high");
  }

  // drive until the magnet shows up
  guard = 0;
  while (readHall(d.pin) < HALL_TRIP) {
    if (d.stepper.runSpeed()) guard++;
    if (guard > HOME_SCAN_LIMIT) return homeFailed(d, "magnet not found");
  }

  // cross the magnet and remember where the reading peaked - that is its centre,
  // unlike the trip point, which drifts with speed and noise
  int  peak    = 0;
  long peakPos = d.stepper.currentPosition();
  long width   = 0;
  int  v;

  while ((v = readHall(d.pin)) >= HALL_TRIP) {
    if (v > peak) { peak = v; peakPos = d.stepper.currentPosition(); }
    if (d.stepper.runSpeed()) width++;
    if (width > HOME_SCAN_LIMIT) return homeFailed(d, "sensor never released");
  }

  // back up into the peak
  d.stepper.setAcceleration(SPEED);
  d.stepper.moveTo(peakPos);
  while (d.stepper.distanceToGo() != 0) d.stepper.run();

  d.stepper.disableOutputs();
  d.stepper.setCurrentPosition(0);
  d.target = 0;
  d.lastFail = 0;

  // The offset is applied as bookkeeping, not as motion: whole flaps just say
  // which number the magnet sits under, so the drum never has to reverse or
  // wind most of a revolution to reach 00. Only the sub-flap remainder is a
  // real move, and that one is always forward and shorter than one flap.
  long  whole = (long)floorf(d.homeOffset);
  float frac  = d.homeOffset - whole;

  d.value = (int)(((-whole) % 60 + 60) % 60);
  if (frac > 0.001f) {
    moveFlaps(d, frac);
    d.stepper.setCurrentPosition(0);
    d.target = 0;
  }

  Serial.printf("%s: homed, peak %d over %ld steps, showing %d\n",
                d.name, peak, width, d.value);
  return true;
}

void gotoNumber(Drum &d, int n)
{
  moveFlaps(d, (n - d.value + 60) % 60);
  d.value = n;
}

// Re-reference against the magnet whenever the number wraps, instead of
// stepping blindly around: nothing accumulates, and in 12 hour mode - where 0
// never comes up and 12 -> 1 would otherwise mean 49 flaps - the drum takes
// the short way through the magnet.
//
// If homing fails the drum position is unknown, so moving would only smear the
// error further. Better to sit still and keep complaining on the serial port.
void showNumber(Drum &d, int n)
{
  if (n <= d.value && !homeDrum(d)) return;

  gotoNumber(d, n);
}
