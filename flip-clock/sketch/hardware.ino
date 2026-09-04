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

void homeDrum(Drum &d)
{
  d.stepper.setMaxSpeed(SPEED);
  d.stepper.setSpeed(d.dir * SPEED);

  long guard = 0;

  // already sitting on the magnet - step off it first
  while (readHall(d.pin) >= HALL_TRIP && guard < 3072) {
    if (d.stepper.runSpeed()) guard++;
  }

  // drive until the magnet shows up
  guard = 0;
  while (readHall(d.pin) < HALL_TRIP) {
    if (d.stepper.runSpeed()) guard++;
    if (guard > 3072) {                      // one and a half revolutions, no magnet
      d.stepper.disableOutputs();
      Serial.printf("[!] %s: magnet not found, ADC=%d\n", d.name, readHall(d.pin));
      return;
    }
  }

  // cross the magnet and remember where the reading peaked - that is its centre,
  // unlike the trip point, which drifts with speed and noise
  int  peak    = 0;
  long peakPos = d.stepper.currentPosition();
  long width   = 0;
  int  v;

  while ((v = readHall(d.pin)) >= HALL_TRIP && width < 3072) {
    if (v > peak) { peak = v; peakPos = d.stepper.currentPosition(); }
    if (d.stepper.runSpeed()) width++;
  }

  // back up into the peak
  d.stepper.setAcceleration(SPEED);
  d.stepper.moveTo(peakPos);
  while (d.stepper.distanceToGo() != 0) d.stepper.run();

  d.stepper.disableOutputs();
  d.stepper.setCurrentPosition(0);
  d.target = 0;
  d.value  = 0;

  if (d.homeOffset != 0) {
    moveFlaps(d, d.homeOffset);
    d.stepper.setCurrentPosition(0);
    d.target = 0;
  }

  Serial.printf("%s: homed, peak %d over %ld steps\n", d.name, peak, width);
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
void showNumber(Drum &d, int n)
{
  if (n <= d.value) homeDrum(d);
  if (n != 0)       gotoNumber(d, n);
}
