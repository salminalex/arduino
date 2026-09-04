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
| `sketch/page.h` | the page CSS and JS, served as `/style.css` and `/app.js` |
| `sketch/config.h` | pins, tuning constants |
| `sketch/secrets.h` | passwords, gitignored — copy from `secrets.h.example` |

Needs the `AccelStepper` library. Everything else ships with the ESP32 core.

## First run

1. Flash, then look for the **FlipClock** WiFi network (passphrase from `secrets.h`)
2. The settings page opens by itself — otherwise go to `192.168.4.1`
3. Pick your network, timezone and 12/24 hour format, save
4. The clock reboots, homes both drums and starts running

Afterwards the same page lives at the board IP or at
[http://flipclock.local](http://flipclock.local), behind a login.

**The timezone is not something you pick.** The browser knows its own offset
and the dates it switches, so the page derives the POSIX string from that — no
list, no lookup, and it works in the portal where there is no internet. Under
*set manually* there is a region/city picker for the case where the clock is a
gift to another timezone; it pulls the full IANA table when the network allows.

Before the first build, copy `sketch/secrets.h.example` to `sketch/secrets.h`
and put your own values in — the portal passphrase and the login for the
settings page. That file is gitignored; the repository never sees it.

The portal is where the password of your real network gets typed in, and WPA2
is what keeps it off the air, so don't leave the example values in place.

## Calibration

Homing finds the *peak* of the hall reading, which is the physical centre of
the magnet. The home offset then says how many flaps sit between that centre
and 00 — the board cannot know this, since it never sees the printed digits.

One flap = 34 motor steps = 6° of drum rotation. Whole flaps are bookkeeping —
they only tell the firmware which number the magnet sits under, so the drum
never reverses and never winds most of a turn to reach 00. A fraction is a real
move: 0.5 nudges the drum by 17 steps.

The settings page prints what the firmware currently believes each drum shows.
Compare that with the window and add the difference to the offset: window one
lower, add 1; one higher, subtract 1. Saving applies the correction to the
running count straight away, so the next minute lands right — no re-homing
needed. Values live in NVS and survive reflashing.

Saving only restarts the board when the network changed. Timezone, time format
and offsets are applied in place and the page stays where it is.

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
- A drum that cannot find its magnet stops instead of stepping from a stale
  reference, and backs off for five minutes rather than winding in circles.
- CSS and JS are served as separate cached files, with a content hash in the
  URL so a reflash is picked up.

## Credits

3D models, both **CC BY-NC 4.0** — print and modify freely, don't sell:

- [Split flap clock](https://makerworld.com/en/models/1045068-split-flap-clock#profileId-1030702) by **Adam** — the original
- [Split Flap Clock ohne Box mit Glow](https://makerworld.com/en/models/2010047-split-flap-clock-ohne-box-mit-glow#profileId-2165169) by **Hirse** — remix without the box

Original firmware: [Adam-Simon1/Split-Flap-Clock](https://github.com/Adam-Simon1/Split-Flap-Clock).
It carries no license file, so it is all-rights-reserved by default — none of
it is copied here.
