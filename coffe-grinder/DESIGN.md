# Coffee Grinder — Design Notes

Motorized Timemore C3 ESP burr grinder. Arduino Nano drives a 42GP-775 gearmotor
through a BTS7960 H-bridge, reads a Hall encoder for closed-loop speed, and shows
state on a 128x64 SPI OLED with three LED-ring buttons.

This file is the durable record of *why* things are the way they are. Measured
numbers, dead ends, and the reasoning behind the control design.

---

## 1. Hardware

| Part | Model | Notes |
|---|---|---|
| Motor | 42GP-775, 24V, 120 RPM, 51:1 gearbox | Torque is sufficient for coffee |
| Driver | Double BTS7960 43A | Reverse + per-side current sense |
| Encoder | 775-P16 Hall, 16 pulses/rev **on the motor shaft** | Divide by 51 for burr RPM |
| Display | OLED 0.96" SSD1306 **SPI 7-pin**, two-color | Yellow band y=0..15, blue y=16..63 |
| Buttons | 3x 12mm momentary with LED ring | INPUT_PULLUP, pressed = LOW |
| PSU | 24V 2.75A (66W) | |
| Buck | LM2596 24V -> 5V | **Set to 4.9V** so USB always wins |
| MCU | Arduino Nano ATmega328P | **Old Bootloader** variant, FTDI FT232R |

### Power rules

- Common ground is mandatory: Arduino GND + LM2596 OUT- + BTS7960 B-.
- `B+/B-` is the power **input**, `M+/M-` is the motor **output**. Swapping these
  was a multi-day debugging disaster: the motor sat across the supply rail, ran
  uncontrollably, cooked the driver through its body diodes, and browned out the
  5V rail (flickering display, failed uploads, dead buttons).
- Any disconnect jumper goes on **+5V, never on ground**. Breaking ground in a
  shared-ground system creates return paths through signal lines.
- LM2596 at 4.9V means USB (5.0V) back-feeds nothing and both sources can be
  connected at once. This is what makes serial debugging possible with the case
  assembled.

---

## 2. Pinout (final, soldered)

Grouped so that neighbouring pins belong to the same module.

```
Buttons + rings
  A3   BTN_START     input pullup
  A2   BTN_MINUS     input pullup
  A4   BTN_PLUS      input pullup
  D10  LED_START     PWM  (ring, breathing)
  D9   LED_PLUSMIN   PWM  (both rings, breathing)

OLED (software SPI)
  D11  MOSI (module label D1)
  D13  SCK  (module label D0)
  A5   DC
  D12  CS
  D8   RESET

Encoder
  D2   A    external interrupt INT0
  D4   B    unused so far, pin change interrupt capable

Motor driver
  D5   RPWM   PWM
  D6   LPWM   PWM
  D7   R_EN
  D3   L_EN
  A0   R_IS   current sense, forward
  A1   L_IS   current sense, reverse
```

Constraints that produced this layout:
- External interrupts exist only on D2/D3 -> encoder A on D2.
- PWM only on D3, D5, D6, D9, D10, D11 -> both LED rings keep PWM breathing.
- D11/D13 are hardware SPI pins; the display is driven in **software SPI**
  anyway, to avoid MISO/SS conflicts.
- `Adafruit_SSD1306(w, h, mosi, clk, dc, rst, cs)` needs a **real GPIO reset
  pin**. Passing `-1` or wiring RES to the Arduino RST pin does not work — the
  library must pulse it LOW->HIGH inside `begin()`.
- On SPI, `display.begin()` **always returns true**. Printing "OLED OK" based on
  its return value proves nothing. Hours were lost to this.

---

## 3. Build and upload

```
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old coffe-grinder
arduino-cli upload -p /dev/cu.usbserial-A5069RR4 \
  --fqbn arduino:avr:nano:cpu=atmega328old coffe-grinder
```

`cpu=atmega328old` (57600 baud bootloader) is mandatory. The normal `atmega328`
setting fails with `not in sync`.

**Before every upload, check nothing else holds the serial port:**

```
ps aux | grep -iE "Arduino IDE|serial-discovery" | grep -v grep
```

Must be empty. The Arduino IDE's `serial-discovery` daemon polls serial ports,
each poll toggles DTR, and DTR is reset on a Nano — the board reboots in a loop
and never syncs. Quit with `osascript -e 'quit app "Arduino IDE"'`; plain `kill`
does not work because Electron respawns the daemon within a second.

Also stop any running log capture (`pkill -f capture.py`) — it holds the port.

Two things once suspected and since **cleared**: the encoder (uploads work fine
with it connected) and Bambu Studio (never opens the port).

---

## 4. Measured data

All measurements at the burr shaft unless stated. `rpm = pulses / 16 / 51` per
minute. Current values are raw 10-bit ADC on R_IS, not amps.

### 4.1 PWM -> RPM, no beans

```
PWM   60  100  120  140  160  180  200  220  240
rpm   25   45   56   65   75   85   94  104  113
```

Dead linear: **0.49 rpm per PWM unit**, i.e. `PWM ~= 2.05 * rpm`. This is the
feed-forward term for the speed controller.

### 4.2 PWM -> RPM, grinding

```
PWM   60  100  120  140  160  180  200  220  240
rpm   23   37   46   55   65   75   84   95  106
```

Load costs roughly 8-10 rpm across the range. Under load the feed-forward
constant is closer to **2.3**.

### 4.3 Load signature (PWM 200 held, 5 g dose)

```
sec  0-4    rpm 95   current 150    empty
sec  5-6                            beans poured, transition takes 1 s
sec  6-21   rpm 86   current 315    grinding
sec 21-23                           beans gone, decay takes 2 s
sec 23-31   rpm 95   current 150    empty again
```

Idle current is rock steady at 148..154. Loaded current wanders 268..337. The
gap never closes, so a threshold around 210 held for 1.5 s is safe.

Throughput: 5 g in 16 s, about **0.3 g/s**, so a normal 18.5 g dose takes
roughly one minute.

`L_IS` reads 0 during forward rotation — only the active half-bridge reports.

### 4.4 Current sense: the aliasing trap

The BTS7960 IS pin carries a current-mirror signal that is **chopped by the
PWM**. A single `analogRead()` samples one arbitrary point of that ~2 ms
waveform. Worse, a 100 ms sample interval is an exact multiple of the 490 Hz PWM
period, so the sample locks to a fixed phase and the error is *systematic*, not
random noise.

Symptom: current appeared to *fall* from 203 to 17 as PWM rose from 60 to 180,
then jump back to 120 at PWM 240. Physically impossible, and it invalidated the
first calibration run.

Fix: average 64 back-to-back reads (~7 ms, several PWM periods) and also track
the peak of that batch. Both are logged. After averaging, current became
monotonic and usable.

The two sense channels are not identical: R_IS reads about 1.5x L_IS for the
same conditions (96 vs 63, later 100 vs 62 — the ratio reproduces). Thresholds
must be kept **per direction**.

---

## 5. Control design

### 5.1 Speed control

PI controller with feed-forward:

```
pwm = target_rpm * PWM_PER_RPM + Kp * err + Ki * integral(err)
```

Feed-forward does the bulk of the work so the controller only trims; this avoids
a long blind ramp. Integral is clamped to stop windup. Output slew is limited
(about 2 s to full speed, 1 s to stop) instead of a separate soft-start routine.

### 5.2 Load detection must not use RPM

Right now load is visible two ways: current rises, and rpm sags from 95 to 86.
**Once the PID is active the rpm sag disappears** — the controller pulls speed
back to target and raises PWM instead.

So in the final firmware the primary load signal is **the PWM the controller
demands to hold target rpm**. It is cleaner than the current sense (no shunt
noise) and needs no calibration. Current stays as a second opinion.

### 5.3 Beans-empty detection: relative, never absolute

Absolute thresholds are only valid for one bean and one grind setting. Coarser
grind = less torque = lower current (confirmed by feel and by physics: less
material removed per revolution). Lighter roasts are denser and harder.

Therefore:

```
threshold = baseline + 0.35 * (plateau - baseline)
```

- `plateau` — the loaded level, measured during the current grind.
- `baseline` — the empty level, measured from the **tail of the previous grind**
  (after auto-stop the motor keeps spinning empty for a couple of seconds) and
  stored in EEPROM.

This self-calibrates on every grind, and it also absorbs driver heating and
supply sag for free. **No separate calibration mode is needed** — a normal grind
already contains every segment we would have measured by hand.

Two edge cases that must be handled or the scheme breaks:

- **First ever run**: nothing learned yet. Seed with the numbers from section 4.3
  but use a deliberately conservative threshold — better to stop late than to
  stop mid-dose. The first complete cycle replaces the seed.
- **Target rpm changed**: baseline taken at 95 rpm is wrong at 60 rpm. Store the
  baseline together with the rpm it was measured at, and mark it stale when the
  target changes.

Instead of a calibration mode, expose a **diagnostics screen** showing baseline,
plateau, computed threshold and live current. When behaviour looks wrong this
immediately separates a threshold problem from a hardware problem.

### 5.4 Jam detection

Not "rpm == 0", and not "rpm dropped sharply".

- A real jam often lets the shaft lurch at 5-10 rpm rather than stopping dead, so
  waiting for zero misses cases.
- Normal grinding is full of brief rpm dips when a large fragment enters the
  burrs. A derivative trigger fires on every one of them.

Condition: **`rpm < 30% of target` AND `PWM > 200`, held for 300 ms.**

- Relative to target, because the same absolute rpm means different things at 60
  and at 110.
- The PWM term is required: low rpm at low PWM just means the user turned the
  speed down.
- 300 ms, not 1.5 s. A stalled motor at full PWM draws locked-rotor current and
  cooks both itself and the driver. For beans-empty, 1.5 s is right because the
  cost of a mistake is only an unfinished dose. For a jam the cost is hardware.

Response: short reverse pulse, pause, forward again, up to 3 attempts, then hard
stop with a message.

Current is a third voice here, used to **tell a jam apart from a broken encoder
wire**. Both look identical (rpm 0 at high PWM), but a jam spikes the current
while a disconnected encoder leaves it at the normal working level, with the
grinder happily turning. Without this check you will hunt for a jam that does not
exist.

### 5.5 Torque limit

At a very fine setting the motor may not have the torque: the PID saturates at
PWM 255 and still misses target rpm. That is physics, not a fault. The firmware
must notice and either report "holding less than requested" or lower the target
itself, otherwise it silently heats the driver chasing an impossible setpoint.

---

## 6. Memory budget

The ATmega328P has 30720 bytes flash and 2048 bytes RAM. The measurement
firmware uses 18180 bytes flash (59%) and 645 bytes of globals.

**The real constraint is RAM.** The SSD1306 frame buffer is 128*64/8 = 1024
bytes, allocated in `begin()`, so it does not appear in the compiler's report.
Actual usage is therefore about 1670 bytes, leaving roughly 380 bytes of stack.

Rules that follow, to be obeyed from the first line of the full firmware:

- All display strings wrapped in `F()`.
- No `snprintf` with `%f` — float formatting drags in heavy code and deep stack.
- Counters and statistics live in EEPROM, not RAM.
- Prefer `int` over `long`, avoid large locals.

---

## 7. Logging workflow

The measurement sketch prints CSV at 115200:
`ms,pwm,rpm,ris,rispk,lis`, one line per 100 ms, with `# step NNN` markers.

Capture with `capture.py <outfile>` (opens the port and sets the baud in one
step). Do **not** use `stty` followed by `cat`: closing the port after `stty`
resets termios back to 9600 and the capture is garbage. Do not read the log file
while it is being written either — one run was lost when the file was replaced
underneath the writer and the data went to an orphaned inode.

Opening the serial port resets the board, which also resets the sketch's mode
selection back to the default.

Keep the measurement sketch in the repository as a separate program. There is no
room for it inside the production firmware, but it will be needed again whenever
new data is required.

---

## 8. Status

Done: all modules verified individually (buttons, display, encoder, both bridge
directions, both current sensors); PWM->RPM curves loaded and unloaded; load
signature with both transitions; a minimal grind firmware with PI control, jam
stop and a load bar.

Next: state machine skeleton (SLEEP / IDLE / GRINDING / JAM / DONE), EEPROM
persistence, self-calibrating thresholds, auto-stop, jam reverse, burr mileage
and shot counters, settings screen, sleep with wake on button.

Open decisions: which settings are editable beyond target rpm, cleaning reminder
threshold, sleep timeout.
