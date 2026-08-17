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

#define RPM_MIN    40
#define RPM_MAX   110
#define RPM_STEP    5

#define SAMPLE_MS   100
#define PWM_PER_RPM 2.7
#define KP          1.5
#define KI          4.0
#define I_LIMIT     30.0

#define SLEW_UP     12
#define SLEW_DOWN   20

#define JAM_RPM     15
#define JAM_MS     1500
#define START_GRACE 1200

#define LOAD_MAX    400

Adafruit_SSD1306 display(128, 64, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

ButtonConfig cfgStart;
AceButton btnStart(&cfgStart, BTN_START);
AceButton btnPlus(BTN_PLUS);
AceButton btnMinus(BTN_MINUS);

volatile long pulses = 0;

int   targetRPM = 80;
float shaftRPM  = 0;
float integral  = 0;
float pwmOut    = 0;

bool running = false;
bool jam     = false;

int   load     = 0;
float loadEMA  = 0;

unsigned long lastSample = 0;
unsigned long lastDraw   = 0;
unsigned long startedAt  = 0;
unsigned long jamSince   = 0;

void countPulse() { pulses++; }

void motorOff() {
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 0);
  pwmOut   = 0;
  integral = 0;
}

void startGrind() {
  jam     = false;
  running = true;
  integral = 0;
  pwmOut   = 0;
  loadEMA  = 0;
  noInterrupts();
  pulses = 0;
  interrupts();
  startedAt  = millis();
  lastSample = millis();
  jamSince   = 0;
}

void stopGrind() {
  running = false;
  jamSince = 0;
}

void handleEvent(AceButton* b, uint8_t event, uint8_t state) {
  bool fire = (event == AceButton::kEventPressed ||
               event == AceButton::kEventRepeatPressed);

  switch (b->getPin()) {
    case BTN_PLUS:
      if (fire && targetRPM < RPM_MAX) targetRPM += RPM_STEP;
      break;
    case BTN_MINUS:
      if (fire && targetRPM > RPM_MIN) targetRPM -= RPM_STEP;
      break;
    case BTN_START:
      if (event == AceButton::kEventPressed) {
        if (running) stopGrind();
        else         startGrind();
      }
      break;
  }
}

void control() {
  unsigned long now = millis();
  unsigned long dt  = now - lastSample;
  if (dt < SAMPLE_MS) return;
  lastSample = now;

  noInterrupts();
  long p = pulses;
  pulses = 0;
  interrupts();

  shaftRPM = (p / PULSES_PER_REV) / (dt / 60000.0) / GEAR_RATIO;

  load    = analogRead(R_IS);
  loadEMA = loadEMA * 0.7 + load * 0.3;

  float want = 0;

  if (running) {
    float err = targetRPM - shaftRPM;
    integral += err * (dt / 1000.0);
    integral = constrain(integral, -I_LIMIT, I_LIMIT);
    want = targetRPM * PWM_PER_RPM + KP * err + KI * integral;
    want = constrain(want, 0, 255);
  }

  if (want > pwmOut) pwmOut = min(pwmOut + SLEW_UP, want);
  else               pwmOut = max(pwmOut - SLEW_DOWN, want);

  analogWrite(LPWM, 0);
  analogWrite(RPWM, (int)pwmOut);

  if (running && now - startedAt > START_GRACE) {
    if (shaftRPM < JAM_RPM && pwmOut > 150) {
      if (jamSince == 0) jamSince = now;
      else if (now - jamSince > JAM_MS) {
        stopGrind();
        motorOff();
        analogWrite(RPWM, 0);
        jam = true;
      }
    } else {
      jamSince = 0;
    }
  }
}

void drawBar(int y, int value, int maxValue) {
  display.drawRect(0, y, 128, 9, SSD1306_WHITE);
  int w = (int)((long)value * 126 / maxValue);
  w = constrain(w, 0, 126);
  if (w > 0) display.fillRect(1, y + 1, w, 7, SSD1306_WHITE);
}

void draw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 4);
  display.print("SET ");
  display.print(targetRPM);

  display.setCursor(74, 4);
  if (jam)          display.print("JAM STOP");
  else if (running) display.print((millis() - startedAt) / 1000);
  else              display.print("READY");

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print((int)shaftRPM);
  display.setTextSize(1);
  display.setCursor(46, 27);
  display.print("rpm");

  display.setCursor(80, 20);
  display.print("pwm");
  display.setCursor(80, 30);
  display.print((int)pwmOut);

  display.setCursor(0, 42);
  display.print("load ");
  display.print((int)loadEMA);

  drawBar(52, (int)loadEMA, LOAD_MAX);

  display.display();
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
  motorOff();
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

  display.begin(SSD1306_SWITCHCAPVCC);
  lastSample = millis();
}

void loop() {
  btnStart.check();
  btnPlus.check();
  btnMinus.check();

  control();

  unsigned long now = millis();
  if (now - lastDraw >= 150) {
    lastDraw = now;
    draw();
  }

  if (running) {
    int b = 40 + 160 * (1 + sin(now / 250.0)) / 2;
    analogWrite(LED_START, b);
  } else {
    analogWrite(LED_START, jam ? 255 : 60);
  }
  analogWrite(LED_PLUSMIN, 30);
}
