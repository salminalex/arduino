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

#define TEST_PWM  80
#define TEST_MS  800

Adafruit_SSD1306 display(128, 64, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

ButtonConfig cfgStart;
AceButton btnStart(&cfgStart, BTN_START);

volatile long pulses  = 0;
volatile long pulsesB = 0;
volatile bool lastB   = false;

int  step   = 0;
bool runNow = false;
long lastB_count = 0;

char title[16]   = "DIAGNOSTICS";
char line1[22]   = "";
char line2[22]   = "";
char verdict[16] = "";
char hint[22]    = "START = step 1";

void countPulse() { pulses++; }

ISR(PCINT2_vect) {
  bool now = (PIND & (1 << PD4)) != 0;
  if (now && !lastB) pulsesB++;
  lastB = now;
}

void resetPulses() {
  noInterrupts();
  pulses = 0;
  pulsesB = 0;
  interrupts();
}

void motorOff() {
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 0);
}

void handleEvent(AceButton* b, uint8_t event, uint8_t state) {
  if (event == AceButton::kEventPressed) runNow = true;
}

void render() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 4);
  display.print(title);

  display.setCursor(0, 20);
  display.print(line1);
  display.setCursor(0, 32);
  display.print(line2);
  display.setCursor(0, 44);
  display.print(verdict);
  display.setCursor(0, 56);
  display.print(hint);

  display.display();
}

void busy(const char* t, const char* what) {
  strncpy(title, t, sizeof(title) - 1);
  line1[0] = line2[0] = verdict[0] = 0;
  strncpy(hint, what, sizeof(hint) - 1);
  render();
}

void finish(const char* t, const char* nextHint) {
  strncpy(title, t, sizeof(title) - 1);
  strncpy(hint, nextHint, sizeof(hint) - 1);
  render();
}

int readSense(uint8_t pin) {
  long sum = 0;
  for (int i = 0; i < 32; i++) sum += analogRead(pin);
  return sum / 32;
}

long spin(uint8_t pwmPin, bool enable, int* senseDuring, uint8_t sensePin) {
  digitalWrite(R_EN, enable ? HIGH : LOW);
  digitalWrite(L_EN, enable ? HIGH : LOW);

  resetPulses();

  for (int v = 0; v <= TEST_PWM; v += 4) {
    analogWrite(pwmPin, v);
    delay(12);
  }

  delay(TEST_MS / 2);
  if (senseDuring) *senseDuring = readSense(sensePin);
  delay(TEST_MS / 2);

  noInterrupts();
  long p = pulses;
  lastB_count = pulsesB;
  interrupts();

  for (int v = TEST_PWM; v >= 0; v -= 4) {
    analogWrite(pwmPin, v);
    delay(8);
  }
  motorOff();
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);

  return p;
}

void stepIdle() {
  motorOff();
  delay(300);

  resetPulses();
  delay(500);
  noInterrupts();
  long p = pulses;
  long b = pulsesB;
  interrupts();

  int r = readSense(R_IS);
  int l = readSense(L_IS);

  snprintf(line1, sizeof(line1), "R_IS %d  L_IS %d", r, l);
  snprintf(line2, sizeof(line2), "idle A %ld B %ld", p, b);

  if (p > 3)               snprintf(verdict, sizeof(verdict), "SPINS IDLE!");
  else if (r > 60 || l > 60) snprintf(verdict, sizeof(verdict), "LEAK?");
  else                     snprintf(verdict, sizeof(verdict), "ok");
}

void stepForward() {
  int before = readSense(R_IS);
  int during = 0;
  long p = spin(RPWM, true, &during, R_IS);
  snprintf(line1, sizeof(line1), "A %ld  B %ld", p, lastB_count);
  snprintf(line2, sizeof(line2), "R_IS %d>%d", before, during);
  snprintf(verdict, sizeof(verdict), "%s", p > 50 ? "RPWM ok" : "NO SPIN!");
}

void stepReverse() {
  int before = readSense(L_IS);
  int during = 0;
  long p = spin(LPWM, true, &during, L_IS);
  snprintf(line1, sizeof(line1), "A %ld  B %ld", p, lastB_count);
  snprintf(line2, sizeof(line2), "L_IS %d>%d", before, during);
  snprintf(verdict, sizeof(verdict), "%s", p > 50 ? "LPWM ok" : "NO SPIN!");
}

void stepEnableOff() {
  long p = spin(RPWM, false, NULL, R_IS);
  snprintf(line1, sizeof(line1), "A %ld  B %ld", p, lastB_count);
  snprintf(line2, sizeof(line2), "must stay 0");
  snprintf(verdict, sizeof(verdict), "%s", p < 20 ? "EN ok" : "EN BAD!");
}

void stepEncoder() {
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);

  resetPulses();

  unsigned long t0 = millis();
  for (int v = 0; v <= 140; v += 4) {
    analogWrite(RPWM, v);
    delay(12);
  }
  delay(1000);

  noInterrupts();
  long p = pulses;
  long b = pulsesB;
  interrupts();
  unsigned long dt = millis() - t0;

  for (int v = 140; v >= 0; v -= 4) {
    analogWrite(RPWM, v);
    delay(8);
  }
  motorOff();

  float mot = (p / PULSES_PER_REV) / (dt / 60000.0);
  float shaft = mot / GEAR_RATIO;

  snprintf(line1, sizeof(line1), "A %ld  B %ld", p, b);
  snprintf(line2, sizeof(line2), "%d / %d rpm", (int)mot, (int)shaft);
  snprintf(verdict, sizeof(verdict), "%s", p > 200 ? "encoder ok" : "NO PULSES!");
}

void setup() {
  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(LED_START, OUTPUT);
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

  lastB   = (PIND & (1 << PD4)) != 0;
  PCICR  |= (1 << PCIE2);
  PCMSK2 |= (1 << PCINT20);

  cfgStart.setEventHandler(handleEvent);

  display.begin(SSD1306_SWITCHCAPVCC);
  render();
}

void loop() {
  btnStart.check();

  analogWrite(LED_START,   60);
  analogWrite(LED_PLUSMIN, 20);

  if (!runNow) return;
  runNow = false;

  switch (step) {
    case 0:
      busy("1 IDLE", "checking...");
      stepIdle();
      finish("1 IDLE", "START = forward");
      break;
    case 1:
      busy("2 FORWARD", "spinning...");
      stepForward();
      finish("2 FORWARD", "START = reverse");
      break;
    case 2:
      busy("3 REVERSE", "spinning...");
      stepReverse();
      finish("3 REVERSE", "START = EN test");
      break;
    case 3:
      busy("4 ENABLE OFF", "must NOT spin");
      stepEnableOff();
      finish("4 ENABLE OFF", "START = encoder");
      break;
    case 4:
      busy("5 ENCODER", "spinning...");
      stepEncoder();
      finish("5 ENCODER", "START = restart");
      break;
  }

  step = (step + 1) % 5;
}
