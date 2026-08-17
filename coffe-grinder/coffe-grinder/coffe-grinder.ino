#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AceButton.h>
#include <EEPROM.h>
#include <avr/sleep.h>

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

#define UNJAM_PWM     150
#define UNJAM_BOOST    40
#define UNJAM_MS      400
#define UNJAM_PAUSE   200
#define UNJAM_DEAD    150
#define UNJAM_TRIES     3
#define UNJAM_RPM_OK   25

#define TORQUE_PWM    245
#define TORQUE_PCT     90
#define TORQUE_MS    2000

#define DONE_MS    4000
#define LOAD_SPAN   200

#define PLATEAU_MIN   60
#define EMPTY_PCT     35
#define EMPTY_MS    1500
#define MAX_GRIND_MS 120000UL

#define BREATH_MS   1500

#define MENU_HOLD   1000
#define MENU_ITEMS     3
#define MENU_IDLE_MS 15000UL

#define EE_MAGIC_ADDR 0
#define EE_RPM_ADDR   1
#define EE_FLAGS_ADDR 2
#define EE_F_EXPERT 0x01
#define EE_MAGIC      0xC0
#define EE_SAVE_MS    3000

#define SLEEP_MS   60000UL

enum State { ST_IDLE, ST_GRINDING, ST_UNJAM, ST_JAM, ST_DONE, ST_MENU };

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

#define STOP_USER 0
#define STOP_AUTO 1
#define STOP_TIME 2

bool armed      = false;
byte stopReason = STOP_USER;
bool eeDirty    = false;
bool unjamSpun  = false;
byte unjamTry   = 0;

int  loadAcc    = 0;
int  rpmAcc     = 0;
int  effRPM     = RPM_DEFAULT;
bool torqueLow  = false;
unsigned long tSatFrom = 0;

bool expert    = false;
bool menuArmed = false;
bool menuEdit  = false;
byte menuSel   = 0;
byte page      = 0;
unsigned long tCombo = 0;

unsigned int  loopCount = 0;
unsigned int  loopHz    = 0;
unsigned long tHz       = 0;

int freeRam() {
  extern int __heap_start, *__brkval;
  int here;
  return (int)&here - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

unsigned long tEmptyFrom = 0;
unsigned long tEeChange  = 0;
unsigned long doseMs     = 0;
unsigned long tActivity  = 0;

float integral = 0;
float pwmWant  = 0;
int   pwmNow   = 0;

unsigned long tState   = 0;
unsigned long tSample  = 0;
unsigned long tRamp    = 0;
unsigned long tJamFrom = 0;
unsigned long tDraw    = 0;
unsigned long tGrace   = 0;
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
  expert = EEPROM.read(EE_FLAGS_ADDR) & EE_F_EXPERT;
}

void saveSettings() {
  EEPROM.update(EE_MAGIC_ADDR, EE_MAGIC);
  EEPROM.update(EE_RPM_ADDR, targetRPM);
  EEPROM.update(EE_FLAGS_ADDR, expert ? EE_F_EXPERT : 0);
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
      stopReason = STOP_USER;
      unjamTry = 0;
      loadAcc  = 0;
      rpmAcc   = 0;
      effRPM   = targetRPM;
      torqueLow = false;
      tSatFrom  = 0;
      tGrace    = millis();
      if (eeDirty) saveSettings();
      noInterrupts();
      pulses = 0;
      interrupts();
      tSample = millis();
      break;
    case ST_UNJAM:
      motorStop();
      unjamSpun = false;
      unjamTry++;
      rpm = 0;
      break;
    case ST_JAM:
      motorStop();
      break;
    case ST_MENU:
      motorStop();
      menuSel   = 0;
      menuArmed = false;
      menuEdit  = false;
      break;
    case ST_DONE:
      pwmWant = 0;
      break;
    case ST_IDLE:
      motorStop();
      rpm  = 0;
      load = 0;
      loadAcc  = 0;
      rpmAcc   = 0;
      unjamTry = 0;
      torqueLow = false;
      break;
  }
}

void resumeGrinding() {
  state    = ST_GRINDING;
  tState   = millis() - grindMs;
  integral = 0;
  pwmWant  = 0;
  pwmNow   = 0;
  tJamFrom = 0;
  tEmptyFrom = 0;
  tSatFrom = 0;
  tGrace   = millis();
  noInterrupts();
  pulses = 0;
  interrupts();
  tSample = millis();
}

int unjamPwm() {
  return constrain(UNJAM_PWM + (unjamTry - 1) * UNJAM_BOOST, 0, 240);
}

void unjamStep() {
  if (state != ST_UNJAM) return;
  unsigned long el = millis() - tState;

  if (el < UNJAM_DEAD) return;

  if (el < UNJAM_DEAD + UNJAM_MS) {
    analogWrite(RPWM, 0);
    analogWrite(LPWM, unjamPwm());
    if (rpm > UNJAM_RPM_OK) unjamSpun = true;
    return;
  }

  analogWrite(LPWM, 0);
  if (el < UNJAM_DEAD + UNJAM_MS + UNJAM_PAUSE) return;

  if (unjamSpun)                 resumeGrinding();
  else if (unjamTry < UNJAM_TRIES) enter(ST_UNJAM);
  else                             enter(ST_JAM);
}

void checkMenuCombo() {
  bool plus  = digitalRead(BTN_PLUS)  == LOW;
  bool minus = digitalRead(BTN_MINUS) == LOW;

  if (state == ST_MENU) {
    if (!plus && !minus) menuArmed = true;
    tCombo = 0;
    return;
  }

  if (state != ST_IDLE || !plus || !minus) {
    tCombo = 0;
    return;
  }

  if (tCombo == 0) tCombo = millis();
  else if (millis() - tCombo > MENU_HOLD) {
    tCombo    = 0;
    tActivity = millis();
    enter(ST_MENU);
  }
}

void handleEvent(AceButton* b, uint8_t event, uint8_t state8) {
  bool fire = (event == AceButton::kEventPressed ||
               event == AceButton::kEventRepeatPressed);
  if (!fire) return;

  tActivity = millis();

  switch (b->getPin()) {
    case BTN_PLUS:
      if (state != ST_MENU) {
        if (expert && event == AceButton::kEventPressed) page ^= 1;
        break;
      }
      if (!menuArmed) break;
      if (!menuEdit) {
        menuSel = (menuSel + 1) % MENU_ITEMS;
      } else if (targetRPM < RPM_MAX) {
        targetRPM += RPM_STEP;
        eeDirty   = true;
        tEeChange = millis();
      }
      break;
    case BTN_MINUS:
      if (state != ST_MENU) {
        if (expert && event == AceButton::kEventPressed) page ^= 1;
        break;
      }
      if (!menuArmed) break;
      if (!menuEdit) {
        menuSel = (menuSel + MENU_ITEMS - 1) % MENU_ITEMS;
      } else if (targetRPM > RPM_MIN) {
        targetRPM -= RPM_STEP;
        eeDirty   = true;
        tEeChange = millis();
      }
      break;
    case BTN_START:
      if (event != AceButton::kEventPressed) break;
      if (state == ST_MENU) {
        if (!menuArmed) break;
        if (menuEdit) { menuEdit = false; break; }
        if      (menuSel == 0) menuEdit = true;
        else if (menuSel == 1) {
          expert    = !expert;
          page      = 0;
          eeDirty   = true;
          tEeChange = millis();
        }
        else enter(ST_IDLE);
        break;
      }
      if (state == ST_GRINDING) {
        doseMs     = grindMs;
        stopReason = STOP_USER;
        enter(ST_DONE);
      }
      else if (state == ST_JAM)   enter(ST_IDLE);
      else if (state == ST_UNJAM) enter(ST_IDLE);
      else if (state == ST_DONE)  enter(ST_IDLE);
      else if (state == ST_IDLE)  enter(ST_GRINDING);
      break;
  }
}

void applyRamp() {
  if (state == ST_UNJAM) return;

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
  if (state != ST_UNJAM) {
    loadEMA  = (loadEMA * 7 + load * 3) / 10;
    loadAcc += (load * 16 - loadAcc) / 10;
    rpmAcc  += (rpm  * 16 - rpmAcc)  / 5;
  }

  if (state != ST_GRINDING) return;

  grindMs = now - tState;

  int err = effRPM - rpm;
  integral += err * (dt / 1000.0);
  integral = constrain(integral, -I_LIMIT, I_LIMIT);
  pwmWant  = effRPM * PWM_PER_RPM + KP * err + KI * integral;
  pwmWant  = constrain(pwmWant, 0, 255);

  if (now - tGrace <= START_GRACE) return;

  if (rpm < (effRPM * JAM_RPM_PCT) / 100 && pwmNow > JAM_PWM) {
    if (tJamFrom == 0) tJamFrom = now;
    else if (now - tJamFrom > JAM_MS) { enter(ST_UNJAM); return; }
  } else {
    tJamFrom = 0;
  }

  if (pwmNow >= TORQUE_PWM && rpm < (effRPM * TORQUE_PCT) / 100) {
    if (tSatFrom == 0) tSatFrom = now;
    else if (now - tSatFrom > TORQUE_MS) {
      if (effRPM > RPM_MIN) {
        effRPM   = max(effRPM - RPM_STEP, RPM_MIN);
        integral = 0;
      }
      torqueLow = true;
      tSatFrom  = now;
    }
  } else {
    tSatFrom = 0;
  }

  if (loadEMA > plateau) plateau = loadEMA;
  if (plateau >= PLATEAU_MIN) armed = true;

  if (armed && loadEMA < (plateau * EMPTY_PCT) / 100) {
    if (tEmptyFrom == 0) tEmptyFrom = now;
    else if (now - tEmptyFrom > EMPTY_MS) {
      doseMs     = grindMs;
      stopReason = STOP_AUTO;
      enter(ST_DONE);
      return;
    }
  } else {
    tEmptyFrom = 0;
  }

  if (grindMs > MAX_GRIND_MS) {
    doseMs     = grindMs;
    stopReason = STOP_TIME;
    enter(ST_DONE);
  }
}

ISR(PCINT1_vect) {}

void goSleep() {
  motorStop();
  digitalWrite(R_EN, LOW);
  digitalWrite(L_EN, LOW);

  analogWrite(LED_START, 0);
  analogWrite(LED_PLUSMIN, 0);
  digitalWrite(LED_START, LOW);
  digitalWrite(LED_PLUSMIN, LOW);

  display.ssd1306_command(SSD1306_DISPLAYOFF);
  if (eeDirty) saveSettings();

  PCMSK1 |= (1 << PCINT11);
  PCICR  |= (1 << PCIE1);

  byte adc = ADCSRA;
  ADCSRA = 0;

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  sleep_enable();
  sei();
  sleep_cpu();
  sleep_disable();

  ADCSRA = adc;
  PCMSK1 &= ~(1 << PCINT11);

  while (digitalRead(BTN_START) == LOW) delay(10);
  delay(50);

  display.ssd1306_command(SSD1306_DISPLAYON);
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);

  tActivity = millis();
  tSample   = millis();
  tDraw     = 0;
  enter(ST_IDLE);
}

void drawBar(int y, int value, int span) {
  display.drawRect(0, y, 128, 9, SSD1306_WHITE);
  int w = (int)((long)value * 126 / span);
  w = constrain(w, 0, 126);
  if (w) display.fillRect(1, y + 1, w, 7, SSD1306_WHITE);
}

void drawMenu() {
  display.setCursor(0, 0);
  display.print(F("SETTINGS"));
  display.drawFastHLine(0, 11, 128, SSD1306_WHITE);

  display.setCursor(0, 18);
  display.print(menuSel == 0 ? '>' : ' ');
  display.setCursor(12, 18);
  display.print(F("SPEED"));
  if (menuEdit) {
    display.fillRect(88, 16, 28, 11, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  }
  display.setCursor(92, 18);
  display.print(targetRPM);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 31);
  display.print(menuSel == 1 ? '>' : ' ');
  display.setCursor(12, 31);
  display.print(F("DETAILS"));
  display.setCursor(92, 31);
  display.print(expert ? F("[x]") : F("[ ]"));

  display.setCursor(0, 44);
  display.print(menuSel == 2 ? '>' : ' ');
  display.setCursor(12, 44);
  display.print(F("EXIT"));

  display.setCursor(0, 56);
  display.print(menuEdit ? F("+/- set   START ok")
                         : F("+/- move  START pick"));
  display.display();
}

void drawPair(int y, const __FlashStringHelper* la, int va,
                     const __FlashStringHelper* lb, int vb) {
  display.setCursor(0, y);
  display.print(la);
  display.print(' ');
  display.print(va);
  display.setCursor(66, y);
  display.print(lb);
  display.print(' ');
  display.print(vb);
}

void drawDiag() {
  display.setCursor(0, 4);
  display.print(F("DIAG"));
  display.setCursor(66, 4);
  display.print(loopHz);
  display.print(F(" Hz"));

  drawPair(18, F("CUR"), current,       F("IDL"), idleAt(pwmNow));
  drawPair(27, F("LOD"), load,          F("PWM"), pwmNow);
  drawPair(36, F("PLT"), plateau,       F("THR"), (plateau * EMPTY_PCT) / 100);
  drawPair(45, F("INT"), (int)integral, F("EFF"), effRPM);
  drawPair(54, F("REV"), unjamTry,      F("RAM"), freeRam());
  display.display();
}

void draw() {
  int rpmShow  = rpmAcc / 16;
  int loadShow = loadAcc / 16;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (state == ST_MENU) {
    drawMenu();
    return;
  }

  if (expert && page == 1 && state != ST_UNJAM && state != ST_JAM) {
    drawDiag();
    return;
  }

  display.setCursor(0, expert ? 4 : 0);
  if (expert) {
    display.print(F("SET "));
    display.print(targetRPM);
    if (torqueLow) {
      display.print('>');
      display.print(effRPM);
    }
  } else if (state == ST_GRINDING) {
    display.print(F("ACTUAL "));
    display.print(rpmShow);
    display.print(F(" rpm"));
  } else {
    display.print(F("TARGET "));
    display.print(targetRPM);
    display.print(F(" rpm"));
  }

  if (state == ST_UNJAM) {
    display.setTextSize(2);
    display.setCursor(0, 26);
    display.print(F("REVERSE"));
    display.setTextSize(1);
    display.setCursor(0, 54);
    display.print(F("clearing "));
    display.print(unjamTry);
    display.print('/');
    display.print(UNJAM_TRIES);
    display.display();
    return;
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

  if (!expert) {
    display.setTextSize(2);
    switch (state) {
      case ST_IDLE:
        display.setCursor(0, 22);
        display.print(F("READY"));
        display.setTextSize(1);
        display.setCursor(0, 56);
        display.print(F("START = grind"));
        break;
      case ST_GRINDING:
        display.setCursor(0, 18);
        display.print(grindMs / 1000);
        display.print('s');
        drawBar(40, loadShow, LOAD_SPAN);
        display.setTextSize(1);
        display.setCursor(0, 56);
        display.print(F("START = stop"));
        break;
      case ST_DONE:
        display.setCursor(0, 18);
        display.print(doseMs / 1000);
        display.print('s');
        display.setTextSize(1);
        display.setCursor(0, 46);
        if      (stopReason == STOP_AUTO) display.print(F("done"));
        else if (stopReason == STOP_TIME) display.print(F("time limit"));
        else                              display.print(F("stopped"));
        break;
      default:
        break;
    }
    display.display();
    return;
  }

  display.setCursor(78, 4);
  switch (state) {
    case ST_IDLE:     display.print(F("READY")); break;
    case ST_GRINDING: display.print(grindMs / 1000); display.print(F("s")); break;
    case ST_DONE:
      if      (stopReason == STOP_AUTO) display.print(F("AUTO "));
      else if (stopReason == STOP_TIME) display.print(F("TIME "));
      else                              display.print(F("STOP "));
      display.print(doseMs / 1000);
      display.print(F("s"));
      break;
    default:
      break;
  }

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(rpmShow);
  display.setTextSize(1);
  display.setCursor(40, 27);
  display.print(F("rpm"));

  display.setCursor(80, 20);
  display.print(F("pwm"));
  display.setCursor(80, 30);
  display.print(pwmNow);

  display.setCursor(0, 42);
  display.print(F("load "));
  display.print(loadShow);
  if (armed) {
    display.print(F(" / "));
    display.print(plateau);
  }

  drawBar(52, loadShow, LOAD_SPAN);
  display.display();
}

int breathe(unsigned long now, int lo, int hi, unsigned int period) {
  float ph = (now % period) * (TWO_PI / period);
  return lo + (int)((hi - lo) * (1.0 - cos(ph)) / 2.0);
}

void updateLeds() {
  unsigned long now = millis();
  int b = 40;

  if      (state == ST_GRINDING) b = breathe(now, 0, 255, BREATH_MS);
  else if (state == ST_UNJAM)    b = (now / 100) % 2 ? 255 : 0;
  else if (state == ST_JAM)      b = (now / 250) % 2 ? 255 : 0;
  else if (state == ST_IDLE)     b = 60;

  int p = 0;
  if (state == ST_MENU) p = menuEdit ? breathe(now, 0, 255, BREATH_MS) : 60;
  else if (tCombo) p = constrain((int)((now - tCombo) * 255UL / MENU_HOLD), 0, 255);

  analogWrite(LED_START, b);
  analogWrite(LED_PLUSMIN, p);
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
  tActivity = millis();
  enter(ST_IDLE);
}

void loop() {
  btnStart.check();
  btnPlus.check();
  btnMinus.check();

  checkMenuCombo();
  controlStep();
  unjamStep();
  applyRamp();

  if (state == ST_DONE && millis() - tState > DONE_MS) enter(ST_IDLE);

  if (state == ST_MENU && millis() - tActivity > MENU_IDLE_MS) enter(ST_IDLE);

  if (eeDirty && state == ST_IDLE && millis() - tEeChange > EE_SAVE_MS) saveSettings();

  if ((state == ST_IDLE || state == ST_MENU) &&
      millis() - tActivity > SLEEP_MS) goSleep();

  loopCount++;
  if (millis() - tHz >= 1000) {
    loopHz    = loopCount;
    loopCount = 0;
    tHz       = millis();
  }

  unsigned long now = millis();
  if (now - tDraw >= 150) {
    tDraw = now;
    draw();
  }

  updateLeds();
}
