# Split-Flap Clock

ESP32 firmware for a two-drum split-flap clock. Two 28BYJ-48 steppers, two
linear hall sensors, NTP time, and a settings page you open from a phone.

Written from scratch for this build — the stock firmware drifted badly and had
no way to configure anything without reflashing.

## Hardware

| | |
|---|---|
| MCU | ESP32 DevKit (30-pin), any WROOM-32 variant |
| Motors | 28BYJ-48 + ULN2003 driver, ×2 |
| Sensors | S49E linear hall, ×2 — **3.3 V only**, 5 V will kill the GPIO |
| Magnets | 3×1 mm, one per drum |
| Power | 5 V 2 A into VIN and both drivers |

Pinout, unchanged from the original project:

| Signal | IN1 | IN2 | IN3 | IN4 | Hall |
|---|---|---|---|---|---|
| Hours | 13 | 12 | 14 | 27 | 34 |
| Minutes | 26 | 25 | 33 | 32 | 35 |

`AccelStepper` in `FULL4WIRE` mode wants the order `(IN1, IN3, IN2, IN4)`, so
IN2 and IN3 look swapped in the constructor. That is deliberate.

On a 30-pin board all ten pins sit in one row and the 4-pin connectors drop
straight on. On a 38-pin board a GND pin sits between GPIO12 and GPIO13, so the
hours cable needs individual leads.

## Files

| | |
|---|---|
| `sketch/sketch.ino` | globals, `setup`, `loop` |
| `sketch/hardware.ino` | steppers, hall sensors, homing |
| `sketch/network.ino` | WiFi, portal, settings page, NVS |
| `sketch/config.h` | pins, tuning constants, passwords |

Needs the `AccelStepper` library. Everything else ships with the ESP32 core.

## First run

1. Flash, then look for the **FlipClock** WiFi network (password in `config.h`)
2. The settings page opens by itself — otherwise go to `192.168.4.1`
3. Pick your network, timezone and 12/24 hour format, save
4. The clock reboots, homes both drums and starts running

Afterwards the same page lives at the board IP or `http://flipclock.local`,
behind a login. On a network with internet the timezone list expands to the
full IANA set and preselects the zone your phone is in.

**Change `AP_PASS`, `UI_USER` and `UI_PASS` in `config.h` before the clock
leaves the house.** The portal is where the password of your real network gets
typed in, and WPA2 is what keeps it off the air.

## Calibration

Homing finds the *peak* of the hall reading, which is the physical centre of
the magnet. The home offset then says how many flaps sit between that centre
and 00 — the board cannot know this, since it never sees the printed digits.

One flap = 34 motor steps = 6° of drum rotation. Halves are allowed.

Press **Re-home drums** on the settings page and look at the window: it should
read 00. Shows 59, add 1. Shows 01, subtract 1. Values live in NVS and survive
reflashing.

If the error *grows* over an hour and resets on the hour, the offset is not the
problem — the drum is binding or the motor is losing steps.

## Notes on the design

- Position is accumulated in `float`. One flap is 34.13 steps, not 34, and
  rounding that away costs 8 steps per revolution.
- The drums re-home whenever the displayed number wraps, so nothing
  accumulates. In 12-hour mode this also stops 12 → 1 from becoming 49 flaps.
- Coils are de-energised after every move. Left on, both motors cook.
- A failed NTP read is never treated as midnight.
- NTP is polled hourly; in between the board runs on its own crystal.

## Credits

3D models, both **CC BY-NC 4.0** — print and modify freely, don't sell:

- [Split flap clock](https://makerworld.com/en/models/1045068-split-flap-clock#profileId-1030702) by **Adam** — the original
- [Split Flap Clock ohne Box mit Glow](https://makerworld.com/en/models/2010047-split-flap-clock-ohne-box-mit-glow#profileId-2165169) by **Hirse** — remix without the box

Original firmware: [Adam-Simon1/Split-Flap-Clock](https://github.com/Adam-Simon1/Split-Flap-Clock).
It carries no license file, so it is all-rights-reserved by default — none of
it is copied here.
