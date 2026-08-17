#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AceButton.h>
#include <EEPROM.h>

using namespace ace_button;

#define BTN_START   A3
#define BTN_MINUS   A2
#define BTN_PLUS    A4
#define LED_START   10
#define LED_PLUSMIN  9

#define OLED_MOSI  11
#define OLED_CLK   13
#define OLED_DC    A5
#define OLED_CS    12
#define OLED_RESET  8

#define ENCODER_A   2
#define ENCODER_B   4

#define RPWM  5
#define LPWM  6
#define R_EN  7
#define L_EN  3
#define R_IS A0
#define L_IS A1

#define PULSES_PER_REV 16
#define GEAR_RATIO     51

#define RPM_MIN    40
#define RPM_MAX   110
#define RPM_STEP    5
#define RPM_DEFAULT 85

#define SAMPLE_MS   100
#define SENSE_N      64

#define PWM_PER_RPM 2.2
#define KP          1.5
#define KI          4.0
#define I_LIMIT     30

#define SLEW_UP     12
#define SLEW_DOWN   20

#define JAM_RPM_PCT  30
#define JAM_PWM     200
#define JAM_MS      300
#define START_GRACE 1500

#define DONE_MS    2000
#define LOAD_SPAN   200

#define PLATEAU_MIN   60
#define EMPTY_PCT     35
#define EMPTY_MS    1500
#define MAX_GRIND_MS 120000UL

#define EE_MAGIC_ADDR 0
#define EE_RPM_ADDR   1
#define EE_MAGIC      0xC0
#define EE_SAVE_MS    3000

enum State { ST_IDLE, ST_GRINDING, ST_JAM, ST_DONE };

Adafruit_SSD1306 display(128, 64, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

ButtonConfig cfgStart;
AceButton btnStart(&cfgStart, BTN_START);
AceButton btnPlus(BTN_PLUS);
AceButton btnMinus(BTN_MINUS);

volatile unsigned int pulses = 0;

State state = ST_IDLE;

int targetRPM = RPM_DEFAULT;
int rpm       = 0;
int current   = 0;
int load      = 0;
int loadEMA   = 0;
int plateau   = 0;

bool armed      = false;
bool autoStop   = false;
bool eeDirty    = false;

unsigned long tEmptyFrom = 0;
unsigned long tEeChange  = 0;
unsigned long doseMs     = 0;

float integral = 0;
float pwmWant  = 0;
int   pwmNow   = 0;

unsigned long tState   = 0;
unsigned long tSample  = 0;
unsigned long tRamp    = 0;
unsigned long tJamFrom = 0;
unsigned long tDraw    = 0;
unsigned long grindMs  = 0;

void countPulse() { pulses++; }

const byte idlePwm[5]  = { 60, 100, 140, 180, 240 };
const byte idleCur[5]  = { 50,  90, 124, 149, 166 };

int idleAt(int pwm) {
  if (pwm <= idlePwm[0]) return idleCur[0];
  for (byte i = 1; i < 5; i++) {
    if (pwm <= idlePwm[i]) {
      int dp = idlePwm[i] - idlePwm[i - 1];
      int dc = idleCur[i] - idleCur[i - 1];
      return idleCur[i - 1] + (long)(pwm - idlePwm[i - 1]) * dc / dp;
    }
  }
  return idleCur[4];
}

void loadSettings() {
  if (EEPROM.read(EE_MAGIC_ADDR) != EE_MAGIC) return;
  int v = EEPROM.read(EE_RPM_ADDR);
  if (v >= RPM_MIN && v <= RPM_MAX) targetRPM = v;
}

void saveSettings() {
  EEPROM.update(EE_MAGIC_ADDR, EE_MAGIC);
  EEPROM.update(EE_RPM_ADDR, targetRPM);
  eeDirty = false;
}

int readCurrent() {
  long sum = 0;
  for (byte i = 0; i < SENSE_N; i++) sum += analogRead(R_IS);
  return (int)(sum / SENSE_N);
}

void motorStop() {
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 0);
  pwmNow   = 0;
  pwmWant  = 0;
  integral = 0;
}

void enter(State s) {
  state  = s;
  tState = millis();

  switch (s) {
    case ST_GRINDING:
      integral = 0;
      pwmWant  = 0;
      tJamFrom = 0;
      tEmptyFrom = 0;
      plateau  = 0;
      loadEMA  = 0;
      armed    = false;
      autoStop = false;
      if (eeDirty) saveSettings();
      noInterrupts();
      pulses = 0;
      interrupts();
      tSample = millis();
      break;
    case ST_JAM:
      motorStop();
      break;
    case ST_DONE:
      pwmWant = 0;
      break;
    case ST_IDLE:
      motorStop();
      rpm  = 0;
      load = 0;
      break;
  }
}

void handleEvent(AceButton* b, uint8_t event, uint8_t state8) {
  bool fire = (event == AceButton::kEventPressed ||
               event == AceButton::kEventRepeatPressed);
  if (!fire) return;

  switch (b->getPin()) {
    case BTN_PLUS:
      if (state == ST_IDLE && targetRPM < RPM_MAX) {
        targetRPM += RPM_STEP;
        eeDirty = true;
        tEeChange = millis();
      }
      break;
    case BTN_MINUS:
      if (state == ST_IDLE && targetRPM > RPM_MIN) {
        targetRPM -= RPM_STEP;
        eeDirty = true;
        tEeChange = millis();
      }
      break;
    case BTN_START:
      if (event != AceButton::kEventPressed) break;
      if (state == ST_GRINDING) {
        doseMs   = grindMs;
        autoStop = false;
        enter(ST_DONE);
      }
      else if (state == ST_JAM)   enter(ST_IDLE);
      else if (state == ST_IDLE)  enter(ST_GRINDING);
      break;
  }
}

void applyRamp() {
  unsigned long now = millis();
  if (now - tRamp < 15) return;
  tRamp = now;

  int want = (state == ST_GRINDING) ? (int)pwmWant : 0;
  if (pwmNow == want) return;

  if (pwmNow < want) pwmNow = min(pwmNow + SLEW_UP, want);
  else               pwmNow = max(pwmNow - SLEW_DOWN, want);

  analogWrite(LPWM, 0);
  analogWrite(RPWM, pwmNow);
}

void controlStep() {
  unsigned long now = millis();
  unsigned int dt = now - tSample;
  if (dt < SAMPLE_MS) return;
  tSample = now;

  noInterrupts();
  unsigned int p = pulses;
  pulses = 0;
  interrupts();

  rpm     = ((long)p * 60000L) / ((long)dt * PULSES_PER_REV * GEAR_RATIO);
  current = readCurrent();
  load    = constrain(current - idleAt(pwmNow), 0, LOAD_SPAN);
  loadEMA = (loadEMA * 7 + load * 3) / 10;

  if (state != ST_GRINDING) return;

  grindMs = now - tState;

  int err = targetRPM - rpm;
  integral += err * (dt / 1000.0);
  integral = constrain(integral, -I_LIMIT, I_LIMIT);
  pwmWant  = targetRPM * PWM_PER_RPM + KP * err + KI * integral;
  pwmWant  = constrain(pwmWant, 0, 255);

  if (grindMs <= START_GRACE) return;

  if (rpm < (targetRPM * JAM_RPM_PCT) / 100 && pwmNow > JAM_PWM) {
    if (tJamFrom == 0) tJamFrom = now;
    else if (now - tJamFrom > JAM_MS) { enter(ST_JAM); return; }
  } else {
    tJamFrom = 0;
  }

  if (loadEMA > plateau) plateau = loadEMA;
  if (plateau >= PLATEAU_MIN) armed = true;

  if (armed && loadEMA < (plateau * EMPTY_PCT) / 100) {
    if (tEmptyFrom == 0) tEmptyFrom = now;
    else if (now - tEmptyFrom > EMPTY_MS) {
      doseMs   = grindMs;
      autoStop = true;
      enter(ST_DONE);
      return;
    }
  } else {
    tEmptyFrom = 0;
  }

  if (grindMs > MAX_GRIND_MS) {
    doseMs = grindMs;
    enter(ST_DONE);
  }
}

void drawBar(int y, int value, int span) {
  display.drawRect(0, y, 128, 9, SSD1306_WHITE);
  int w = (int)((long)value * 126 / span);
  w = constrain(w, 0, 126);
  if (w) display.fillRect(1, y + 1, w, 7, SSD1306_WHITE);
}

void draw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 4);
  display.print(F("SET "));
  display.print(targetRPM);

  display.setCursor(78, 4);
  switch (state) {
    case ST_IDLE:     display.print(F("READY")); break;
    case ST_GRINDING: display.print(grindMs / 1000); display.print(F("s")); break;
    case ST_JAM:      display.print(F("JAM")); break;
    case ST_DONE:
      display.print(autoStop ? F("AUTO ") : F("STOP "));
      display.print(doseMs / 1000);
      display.print(F("s"));
      break;
  }

  if (state == ST_JAM) {
    display.setTextSize(2);
    display.setCursor(0, 26);
    display.print(F("JAMMED"));
    display.setTextSize(1);
    display.setCursor(0, 54);
    display.print(F("START = clear"));
    display.display();
    return;
  }

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(rpm);
  display.setTextSize(1);
  display.setCursor(40, 27);
  display.print(F("rpm"));

  display.setCursor(80, 20);
  display.print(F("pwm"));
  display.setCursor(80, 30);
  display.print(pwmNow);

  display.setCursor(0, 42);
  display.print(F("load "));
  display.print(loadEMA);
  if (armed) {
    display.print(F(" / "));
    display.print(plateau);
  }

  drawBar(52, loadEMA, LOAD_SPAN);
  display.display();
}

void updateLeds() {
  unsigned long now = millis();
  int b = 40;

  if (state == ST_GRINDING) b = 40 + 160 * (1 + sin(now / 250.0)) / 2;
  else if (state == ST_JAM) b = (now / 250) % 2 ? 255 : 0;
  else if (state == ST_IDLE) b = 60;

  analogWrite(LED_START, b);
  analogWrite(LED_PLUSMIN, state == ST_IDLE ? 30 : 10);
}

void setup() {
  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_PLUS,  INPUT_PULLUP);
  pinMode(BTN_MINUS, INPUT_PULLUP);
  pinMode(LED_START,   OUTPUT);
  pinMode(LED_PLUSMIN, OUTPUT);

  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);
  motorStop();
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);

  pinMode(ENCODER_A, INPUT_PULLUP);
  pinMode(ENCODER_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_A), countPulse, RISING);

  ButtonConfig* cfg = ButtonConfig::getSystemButtonConfig();
  cfg->setEventHandler(handleEvent);
  cfg->setFeature(ButtonConfig::kFeatureRepeatPress);
  cfg->setRepeatPressDelay(400);
  cfg->setRepeatPressInterval(120);

  cfgStart.setEventHandler(handleEvent);

  loadSettings();
  display.begin(SSD1306_SWITCHCAPVCC);
  enter(ST_IDLE);
}

void loop() {
  btnStart.check();
  btnPlus.check();
  btnMinus.check();

  controlStep();
  applyRamp();

  if (state == ST_DONE && millis() - tState > DONE_MS) enter(ST_IDLE);

  if (eeDirty && state == ST_IDLE && millis() - tEeChange > EE_SAVE_MS) saveSettings();

  unsigned long now = millis();
  if (now - tDraw >= 150) {
    tDraw = now;
    draw();
  }

  updateLeds();
}
