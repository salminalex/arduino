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

#define PULSES_PER_REV 16.0
#define GEAR_RATIO     51.0
#define WINDOW_MS      250

#define PWM_STEP        5
#define RAMP_STEP       3
#define RAMP_INTERVAL  15

Adafruit_SSD1306 display(128, 64, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

ButtonConfig cfgStart;
AceButton btnStart(&cfgStart, BTN_START);
AceButton btnPlus(BTN_PLUS);
AceButton btnMinus(BTN_MINUS);

volatile long pulses = 0;

unsigned long lastWindow = 0;
unsigned long lastRamp   = 0;

float shaftRPM = 0;
float motorRPM = 0;

long isRsum = 0, isLsum = 0;
int  isCount = 0;
int  isR = 0, isL = 0;
int  isRpeak = 0;

int  targetPWM  = 60;
int  currentPWM = 0;
bool motorOn    = false;
bool dirty      = true;

void countPulse() { pulses++; }

void applyMotor() {
  analogWrite(LPWM, 0);
  analogWrite(RPWM, currentPWM);
}

void handleEvent(AceButton* b, uint8_t event, uint8_t state) {
  bool fire = (event == AceButton::kEventPressed ||
               event == AceButton::kEventRepeatPressed);

  switch (b->getPin()) {
    case BTN_PLUS:
      if (fire) { targetPWM = min(targetPWM + PWM_STEP, 255); dirty = true; }
      break;
    case BTN_MINUS:
      if (fire) { targetPWM = max(targetPWM - PWM_STEP, 0); dirty = true; }
      break;
    case BTN_START:
      if (event == AceButton::kEventReleased) {
        motorOn = !motorOn;
        if (motorOn) isRpeak = 0;
        dirty = true;
      }
      break;
  }
}

void updateRamp() {
  unsigned long now = millis();
  if (now - lastRamp < RAMP_INTERVAL) return;
  lastRamp = now;

  int want = motorOn ? targetPWM : 0;
  if (currentPWM == want) return;

  if (currentPWM < want) currentPWM = min(currentPWM + RAMP_STEP, want);
  else                   currentPWM = max(currentPWM - RAMP_STEP, want);

  applyMotor();
  dirty = true;
}

void sampleCurrent() {
  isRsum += analogRead(R_IS);
  isLsum += analogRead(L_IS);
  isCount++;
}

void updateWindow() {
  unsigned long now = millis();
  if (now - lastWindow < WINDOW_MS) return;

  noInterrupts();
  long p = pulses;
  pulses = 0;
  interrupts();

  float minutes = (now - lastWindow) / 60000.0;
  motorRPM = (p / PULSES_PER_REV) / minutes;
  shaftRPM = motorRPM / GEAR_RATIO;
  lastWindow = now;

  if (isCount > 0) {
    isR = isRsum / isCount;
    isL = isLsum / isCount;
    if (isR > isRpeak) isRpeak = isR;
  }
  isRsum = isLsum = 0;
  isCount = 0;

  dirty = true;
}

void draw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 4);
  display.print(motorOn ? "RUN " : "STOP");
  display.setCursor(52, 4);
  display.print("set ");
  display.print(targetPWM);

  display.setTextSize(3);
  display.setCursor(0, 18);
  display.print(shaftRPM, 0);
  display.setTextSize(1);
  display.setCursor(66, 26);
  display.print("RPM");

  display.setCursor(0, 44);
  display.print("pwm ");
  display.print(currentPWM);
  display.setCursor(66, 44);
  display.print("mot ");
  display.print(motorRPM, 0);

  display.setCursor(0, 54);
  display.print("R ");
  display.print(isR);
  display.setCursor(40, 54);
  display.print("L ");
  display.print(isL);
  display.setCursor(80, 54);
  display.print("pk ");
  display.print(isRpeak);

  int barW = map(isR, 0, 400, 0, 128);
  barW = constrain(barW, 0, 128);
  display.drawFastHLine(0, 63, barW, SSD1306_WHITE);

  display.display();
}

void setup() {
  pinMode(BTN_START,   INPUT_PULLUP);
  pinMode(BTN_PLUS,    INPUT_PULLUP);
  pinMode(BTN_MINUS,   INPUT_PULLUP);
  pinMode(LED_START,   OUTPUT);
  pinMode(LED_PLUSMIN, OUTPUT);

  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 0);
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
  cfgStart.setFeature(ButtonConfig::kFeatureLongPress);
  cfgStart.setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);

  display.begin(SSD1306_SWITCHCAPVCC);
  lastWindow = millis();
}

void loop() {
  btnStart.check();
  btnPlus.check();
  btnMinus.check();

  sampleCurrent();
  updateRamp();
  updateWindow();

  if (dirty) {
    draw();
    dirty = false;
  }

  analogWrite(LED_START,   motorOn ? 200 : 40);
  analogWrite(LED_PLUSMIN, 40);
}
