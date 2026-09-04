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

### When avrdude fails anyway

There is a second, independent failure mode: the IDE is closed, `lsof` shows the
port free, the board is visibly running (display, buttons) — and `arduino-cli
upload` still reports `not in sync: resp=0x00` ten times in a row. Here avrdude
itself is at fault; it misses the bootloader window on this board.

To tell the two apart, talk to the bootloader by hand: pulse DTR+RTS through
`TIOCMBIS`/`TIOCMBIC` and immediately send `0 ` (`0x30 0x20`) at 57600. A reply
of `14 10` (INSYNC + OK) means the board and its bootloader are fine and only
avrdude is failing.

`flash.py` is the workaround and now the primary upload path — it speaks STK500v1
directly and hits the window reliably:

```
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old \
  --output-dir build coffe-grinder
python3 flash.py build/coffe-grinder.ino.hex
```

It resets the board, checks the signature is `1e950f` (ATmega328P) before writing
anything, programs 128-byte pages, reads every page back to verify, then leaves
programming mode so the sketch starts. About a minute for 20 KB.

---

## 4. Measured data

All measurements at the burr shaft unless stated. `rpm = pulses / 16 / 51` per
minute. Current values are raw 10-bit ADC on R_IS, not amps.

Note: the current column of `log-1-idle-sweep.csv` predates the averaging fix and
is invalid. Use `log-4-idle-sweep.csv` for unloaded current.

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

### 4.4 Current vs speed, and the coffee contribution

Both sweeps with 64-sample averaging. `delta` is the work the coffee actually
costs at that speed.

```
PWM      60   80  100  120  140  160  180  200  220  240
idle     50   72   90  106  124  139  149  154  158  166
loaded   77  151  195  236  277  302  323  337  327  342
delta    27   79  106  130  153  164  174  182  169  176
```

Cross-check: idle at PWM 200 reads 154 here and 150 in the hold run of 4.3 —
two different firmware builds on different runs, same number. The method is
sound.

**Baseline is not linear in speed.** From 27 to 95 rpm it climbs about 1.5 units
per rpm, above that only 0.7. Rescaling a stored baseline with a single linear
factor would be wrong at the ends, so the firmware carries a five-point table
with interpolation instead — about twenty bytes of flash for an exact answer.

**The coffee contribution saturates near 175 units above PWM 180.** Past that
point extra speed does not extract more work from the burrs, it only pushes
beans through faster. Consistent with 70-90 rpm being the sensible working
range, with torque headroom left over.

**The batch peak is useless — do not carry it into the production firmware.** At
idle it *falls* from 435 to 309 as PWM rises, so it tracks switching transients,
not motor current. Only the average is meaningful.

### 4.5 Current sense: the aliasing trap

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
  stored in EEPROM. When the target speed changes, rescale it through the
  five-point idle table of 4.4 rather than a linear factor.

This self-calibrates on every grind, and it also absorbs driver heating and
supply sag for free. **No separate calibration mode is needed** — a normal grind
already contains every segment we would have measured by hand.

Two edge cases that must be handled or the scheme breaks:

- **First ever run**: nothing learned yet. Seed with the numbers from section 4.3
  but use a deliberately conservative threshold — better to stop late than to
  stop mid-dose. The first complete cycle replaces the seed.
- **Target rpm changed**: baseline taken at 95 rpm is wrong at 60 rpm. Store the
  baseline together with the rpm it was measured at, and when the target moves,
  shift it by the difference the idle table of 4.4 predicts between those two
  speeds. No blind grind is needed.

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

Response, as built (`ST_UNJAM`): stop the motor, wait 150 ms of dead time, drive
backwards at PWM 150 for 400 ms, coast 200 ms, then decide from the encoder.
Turned during the pulse (rpm > 25) — go back to grinding; did not — retry with
40 more PWM, up to 3 attempts, then `ST_JAM` and a manual reset. START aborts at
any point. Reverse was validated on 2026-08-16 with a single pulse from IDLE in a
throwaway build, not by jamming the grinder on purpose.

Three details that are easy to get wrong:

- **Dead time before reversing.** Never cross-drive the bridge. Both PWM pins go
  to 0 and stay there for 150 ms so the current in the winding decays before the
  other half turns on.
- **Resume, do not restart.** A recovered jam returns to grinding with elapsed
  time, load plateau and the `armed` flag intact. Calling the normal grinding
  entry instead would zero the plateau, and auto-stop would lose the baseline it
  needs — the next load drop would read as beans-empty and end the dose early.
- **Freeze the load EMA while reversing.** Current is sensed on `R_IS`, the
  forward half of the bridge, so it reads near zero during a reverse pulse. Left
  running, the EMA would decay to almost nothing over the ~750 ms sequence and
  trip the beans-empty test right after resuming.

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

As built: if PWM stays at 245 or above while rpm sits below 90% of the working
setpoint for 2 s, the working setpoint drops by 5 rpm and the integral is
cleared. Repeats down to `RPM_MIN`. Expert view shows it as `SET 85>75`.

Only the *working* setpoint moves. `targetRPM` and the EEPROM copy are untouched,
so the next grind starts from what the user actually asked for — otherwise one
hard bean would silently redefine the setting. It also never climbs back during a
grind: raise it, hit the limit, lower it again is a loop with no payoff over a
16 s dose.

Jam detection uses the working setpoint too. Left on the user's target, the 30%
threshold would grow stale the moment the limiter stepped down.

### 5.6 Grace after resume, not just after start

Jam detection needs a blind window at the start of a grind — the motor spends
about a second reaching speed, and until then "high PWM, low rpm" is simply what
spin-up looks like. `START_GRACE` covers that at 1.5 s.

**The same window is required after a jam recovery, and its absence was a real
bug.** On resume PWM ramps from zero and crosses the jam threshold of 200 in a
quarter second, while rpm still needs about a second. The jam test therefore
fired on its own roughly 550 ms after every recovery — one real bean fragment
would burn all three attempts and end in `JAMMED` with the burrs already clear.
The grace timer is now re-armed by both entry paths, not derived from elapsed
grind time.

---

## 5.7 Interface

Two levels, because the grinder is shared. **Simple** is the default: target rpm,
a big state word, elapsed seconds, a load bar while grinding, and a one-line hint
at the bottom. No PWM, no load numbers. The top line switches from `TARGET 85
rpm` at rest to `ACTUAL 84 rpm` while grinding. **Expert** restores the full
readout and is a checkbox in settings, stored in EEPROM.

Settings open by holding `+` and `−` together for 1 s from READY, and hold three
items: SPEED, DETAILS and EXIT. START enters value editing on SPEED; `+`/`−` then
change the value instead of moving the cursor. The menu exits by itself after 15 s
idle so nobody gets stranded in it.

In expert mode `+`/`−` are otherwise free, so a single press flips to a **DIAG**
page: loop rate, raw current next to the interpolated idle baseline, load and
PWM, plateau next to the auto-stop threshold, the PI integral, the working
setpoint, reverse attempts and free RAM. Loop rate is the one to watch — software
SPI pushes a full kilobyte to the panel and `readCurrent` takes 64 back-to-back
ADC samples, and both block the control loop.

Button rings carry state on their own: dark when the button does nothing, ramping
up as the settings combo is held, steady in the menu, breathing while grinding or
while editing a value. `breathe()` drives every pulse from one period constant.
Its phase comes from `now % period` rather than raw `millis()` — a float carries
24 bits of mantissa, so feeding it a growing millisecond count makes the fade
stutter after about five hours of uptime.

**The panel is two-colour: rows 0-15 are yellow, the rest blue, with a physical
seam between them.** Any 8-pixel text row placed at y=10..17 straddles the seam
and renders visibly torn. Keep headers at y≤7 and body content at y≥18.

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
directions, both current sensors); PWM->RPM curves loaded and unloaded; current
vs speed for both states; load signature with both transitions; a minimal grind
firmware with PI control, jam stop and a load bar.

**Measurement phase is complete** — everything the planned features need has been
captured. The one deliberate omission is reverse under load: jamming the grinder
on purpose to measure it is not worth the risk, so reverse gets validated with a
short low-PWM pulse while the jam recovery is being written.

Firmware built and verified on hardware since: the state machine (IDLE /
GRINDING / UNJAM / JAM / DONE), auto-stop on the beans-empty plateau drop, target
rpm persisted in EEPROM, power-down sleep with wake on START, and jam recovery by
reverse pulse. A real 5 g dose has been ground end to end on this firmware.

Sleep details: `SLEEP_MODE_PWR_DOWN` entered after `SLEEP_MS` idle, woken by
PCINT11 on A3 (START). The ADC is switched off before sleeping and restored
after; the display goes to `SSD1306_DISPLAYOFF`, both bridge enables drop, and
pending EEPROM writes are flushed first. `millis()` stops during power-down, so
every timer is re-based on wake. The ISR body is empty — waking is the point.

**Sleep timeout is 60 s** (tested at 20 s, 5 min judged too long in use).

Also built: the torque limit of 5.5, the two-level interface of 5.7 with its
settings menu and DIAG page, and the resume grace of 5.6.

Next: burr mileage and shot counters in EEPROM, cleaning reminder.

Still unverified on hardware: jam detection itself. Reverse works and the state
machine around it is exercised, but no real jam has ever been provoked — the
thresholds come from measurement, not from a stone in the burrs.

Open decisions: which settings are editable beyond target rpm, cleaning reminder
threshold.
