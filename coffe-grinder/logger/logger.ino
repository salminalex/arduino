#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AceButton.h>

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

#define SAMPLE_MS   100
#define SENSE_N      64

#define SWEEP_FROM   60
#define SWEEP_TO    240
#define SWEEP_STEP   20
#define SWEEP_HOLD 1500

#define HOLD_PWM    200
#define HOLD_MAX_MS 90000

#define RAMP_MS      15
#define RAMP_UP       6
#define RAMP_DOWN    20

#define STALL_RPM    10
#define STALL_MS   1500

#define MODE_SWEEP 0
#define MODE_HOLD  1

Adafruit_SSD1306 display(128, 64, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

ButtonConfig cfgStart;
AceButton btnStart(&cfgStart, BTN_START);
AceButton btnPlus(BTN_PLUS);
AceButton btnMinus(BTN_MINUS);

volatile unsigned int pulses = 0;

byte mode    = MODE_SWEEP;
bool running = false;
bool stalled = false;

int pwmTarget = 0;
int pwmNow    = 0;
int rpm       = 0;
int ris       = 0;
int risPk     = 0;
int lis       = 0;

int rpmMax  = 0;
int risMax  = 0;
int sweepIx = 0;

unsigned long tStart     = 0;
unsigned long tSample    = 0;
unsigned long tRamp      = 0;
unsigned long tStep      = 0;
unsigned long tStallFrom = 0;
unsigned long tDraw      = 0;

void countPulse() { pulses++; }

int sensePeak = 0;

int senseAvg(uint8_t pin, byte n) {
  long sum = 0;
  int  mx  = 0;
  for (byte i = 0; i < n; i++) {
    int v = analogRead(pin);
    sum += v;
    if (v > mx) mx = v;
  }
  sensePeak = mx;
  return (int)(sum / n);
}

void motorOff() {
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 0);
  pwmNow    = 0;
  pwmTarget = 0;
}

void runStart() {
  stalled = false;
  running = true;
  rpmMax  = 0;
  risMax  = 0;
  sweepIx = 0;
  noInterrupts();
  pulses = 0;
  interrupts();
  tStart     = millis();
  tSample    = tStart;
  tStep      = tStart;
  tStallFrom = 0;
  pwmTarget  = (mode == MODE_SWEEP) ? SWEEP_FROM : HOLD_PWM;
  Serial.println(F("# ms,pwm,rpm,ris,rispk,lis"));
}

void runStop() {
  running   = false;
  pwmTarget = 0;
  Serial.print(F("# end rpmMax="));
  Serial.print(rpmMax);
  Serial.print(F(" risMax="));
  Serial.print(risMax);
  Serial.print(F(" ms="));
  Serial.println(millis() - tStart);
}

void handleEvent(AceButton* b, uint8_t event, uint8_t state) {
  if (event != AceButton::kEventPressed) return;

  switch (b->getPin()) {
    case BTN_START:
      if (running) runStop();
      else         runStart();
      break;
    case BTN_PLUS:
    case BTN_MINUS:
      if (!running) mode = (mode == MODE_SWEEP) ? MODE_HOLD : MODE_SWEEP;
      break;
  }
}

void ramp() {
  unsigned long now = millis();
  if (now - tRamp < RAMP_MS) return;
  tRamp = now;

  if (pwmNow == pwmTarget) return;
  if (pwmNow < pwmTarget) pwmNow = min(pwmNow + RAMP_UP, pwmTarget);
  else                    pwmNow = max(pwmNow - RAMP_DOWN, pwmTarget);

  analogWrite(LPWM, 0);
  analogWrite(RPWM, pwmNow);
}

void sample() {
  unsigned long now = millis();
  unsigned int dt = now - tSample;
  if (dt < SAMPLE_MS) return;
  tSample = now;

  noInterrupts();
  unsigned int p = pulses;
  pulses = 0;
  interrupts();

  rpm = ((long)p * 60000L) / ((long)dt * PULSES_PER_REV * GEAR_RATIO);
  ris   = senseAvg(R_IS, SENSE_N);
  risPk = sensePeak;
  lis   = senseAvg(L_IS, 16);

  if (rpm > rpmMax) rpmMax = rpm;
  if (ris > risMax) risMax = ris;

  if (!running) return;

  Serial.print(now - tStart);
  Serial.print(',');
  Serial.print(pwmNow);
  Serial.print(',');
  Serial.print(rpm);
  Serial.print(',');
  Serial.print(ris);
  Serial.print(',');
  Serial.print(risPk);
  Serial.print(',');
  Serial.println(lis);

  if (pwmNow > 120 && rpm < STALL_RPM) {
    if (tStallFrom == 0) tStallFrom = now;
    else if (now - tStallFrom > STALL_MS) {
      runStop();
      motorOff();
      stalled = true;
      Serial.println(F("# STALL"));
    }
  } else {
    tStallFrom = 0;
  }
}

void schedule() {
  if (!running) return;
  unsigned long now = millis();

  if (mode == MODE_SWEEP) {
    if (now - tStep >= SWEEP_HOLD) {
      tStep = now;
      sweepIx++;
      int v = SWEEP_FROM + sweepIx * SWEEP_STEP;
      if (v > SWEEP_TO) { runStop(); return; }
      pwmTarget = v;
      Serial.print(F("# step "));
      Serial.println(v);
    }
  } else {
    if (now - tStart >= HOLD_MAX_MS) runStop();
  }
}

void draw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 4);
  if (stalled)      display.print(F("STALL"));
  else if (running) display.print(F("REC"));
  else              display.print(mode == MODE_SWEEP ? F("SWEEP") : F("HOLD"));

  display.setCursor(60, 4);
  if (running) display.print((millis() - tStart) / 1000);

  display.setCursor(96, 4);
  display.print(F("pwm"));

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(rpm);
  display.setCursor(80, 20);
  display.print(pwmNow);

  display.setTextSize(1);
  display.setCursor(0, 42);
  display.print(F("R "));
  display.print(ris);
  display.print(F(" pk "));
  display.print(risPk);

  display.setCursor(0, 54);
  display.print(F("max "));
  display.print(rpmMax);
  display.print(F(" / "));
  display.print(risMax);

  display.display();
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_PLUS,  INPUT_PULLUP);
  pinMode(BTN_MINUS, INPUT_PULLUP);
  pinMode(LED_START,   OUTPUT);
  pinMode(LED_PLUSMIN, OUTPUT);

  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);
  motorOff();
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);

  pinMode(ENCODER_A, INPUT_PULLUP);
  pinMode(ENCODER_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_A), countPulse, RISING);

  ButtonConfig* cfg = ButtonConfig::getSystemButtonConfig();
  cfg->setEventHandler(handleEvent);
  cfgStart.setEventHandler(handleEvent);

  display.begin(SSD1306_SWITCHCAPVCC);
  Serial.println(F("# ready"));
}

void loop() {
  btnStart.check();
  btnPlus.check();
  btnMinus.check();

  ramp();
  sample();
  schedule();

  unsigned long now = millis();
  if (now - tDraw >= 200) {
    tDraw = now;
    draw();
  }

  analogWrite(LED_START,   running ? 200 : 50);
  analogWrite(LED_PLUSMIN, 30);
}
