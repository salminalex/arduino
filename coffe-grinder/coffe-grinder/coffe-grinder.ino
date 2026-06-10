#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PID_v1.h>
#include "buttons.h"

// ── Pins ─────────────────────────────────────────────────────────────────────
#define OLED_CS    8
#define OLED_DC    9
#define OLED_RESET 10

#define RPWM  5
#define LPWM  6
#define R_EN  4
#define L_EN  7

#define ENCODER_A 2
#define ENCODER_B 3

#define BTN_START A0
#define BTN_PLUS  A1
#define BTN_MINUS A2

// ── Grinding config ───────────────────────────────────────────────────────────
#define GEAR_RATIO       51.0
#define PULSES_PER_REV   16.0
#define RPM_CALC_MS      300

#define TARGET_RPM_MIN   80
#define TARGET_RPM_MAX   130
#define TARGET_RPM_DEF   105

#define STALL_RPM        5.0
#define STALL_DELAY_MS   2000
#define FINISH_RPM_DELTA 15.0
#define FINISH_HOLD_MS   800

#define REVERSE_DURATION 400
#define RAMP_STEP_DELAY  15

// ── State machine ─────────────────────────────────────────────────────────────
enum State { IDLE, GRINDING, STALL_RECOVERY, DONE };
State state = IDLE;

// ── PID ───────────────────────────────────────────────────────────────────────
double pidInput = 0, pidOutput = 0, pidSetpoint = TARGET_RPM_DEF;
PID grindPID(&pidInput, &pidOutput, &pidSetpoint, 4.0, 1.2, 0.3, DIRECT);

// ── Encoder ──────────────────────────────────────────────────────────────────
volatile long pulseCount = 0;
unsigned long lastRpmCalc = 0;
float rpm = 0;

void countPulse() { pulseCount++; }

// ── Display ───────────────────────────────────────────────────────────────────
Adafruit_SSD1306 display(128, 64, &SPI, OLED_DC, OLED_RESET, OLED_CS);

// ── Buttons ───────────────────────────────────────────────────────────────────
Button btnStart = {BTN_START, HIGH, 0};
Button btnPlus  = {BTN_PLUS,  HIGH, 0};
Button btnMinus = {BTN_MINUS, HIGH, 0};

// ── Motor control ─────────────────────────────────────────────────────────────
int currentPWM = 0;
int targetRPM  = TARGET_RPM_DEF;

void setMotor(int pwm) {
  currentPWM = constrain(pwm, 0, 255);
  analogWrite(RPWM, currentPWM);
  analogWrite(LPWM, 0);
}

void stopMotor() {
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 0);
  currentPWM = 0;
}

void reverseBlip() {
  stopMotor();
  delay(100);
  analogWrite(LPWM, 180);
  delay(REVERSE_DURATION);
  analogWrite(LPWM, 0);
  delay(150);
}

void rampTo(int target) {
  if (currentPWM < target) {
    for (int i = currentPWM; i <= target; i += 3) {
      analogWrite(RPWM, i);
      delay(RAMP_STEP_DELAY);
    }
  } else {
    for (int i = currentPWM; i >= target; i -= 3) {
      analogWrite(RPWM, i);
      delay(RAMP_STEP_DELAY);
    }
  }
  currentPWM = target;
}

// ── RPM calculation ───────────────────────────────────────────────────────────
void calcRPM() {
  unsigned long now = millis();
  if (now - lastRpmCalc < RPM_CALC_MS) return;

  noInterrupts();
  long p = pulseCount;
  pulseCount = 0;
  interrupts();

  float elapsed = (now - lastRpmCalc) / 60000.0;
  rpm = (p / PULSES_PER_REV) / elapsed / GEAR_RATIO;
  lastRpmCalc = now;
}

// ── Display ───────────────────────────────────────────────────────────────────
// Yellow zone: y 0-15  Blue zone: y 16-63
void drawDisplay(const char *label) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Yellow zone — status + target RPM
  display.setTextSize(1);
  display.setCursor(0, 4);
  display.print(label);
  display.setCursor(80, 4);
  display.print(">");
  display.print(targetRPM);
  display.print(" RPM");

  // Blue zone — current RPM big
  display.setTextSize(3);
  display.setCursor(0, 18);
  display.print((int)rpm);

  // PWM bar bottom
  display.setTextSize(1);
  display.setCursor(0, 52);
  display.print("PWM:");
  display.print(currentPWM);
  int barW = map(currentPWM, 0, 255, 0, 75);
  display.fillRect(50, 54, barW, 6, SSD1306_WHITE);
  display.drawRect(50, 54, 75, 6, SSD1306_WHITE);

  display.display();
}

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);

  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);

  pinMode(ENCODER_A, INPUT_PULLUP);
  pinMode(ENCODER_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_A), countPulse, RISING);

  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_PLUS,  INPUT_PULLUP);
  pinMode(BTN_MINUS, INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  // Yellow zone
  display.setCursor(0, 4);
  display.print("COFFEE GRINDER");
  // Blue zone
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print("Starting..");
  display.display();
  delay(1500);

  pidSetpoint = targetRPM;
  grindPID.SetMode(AUTOMATIC);
  grindPID.SetOutputLimits(60, 255);
  grindPID.SetSampleTime(RPM_CALC_MS);

  lastRpmCalc = millis();

  // Auto-start (no buttons yet)
  pulseCount = 0;
  rampTo(80);
  state = GRINDING;
}

// ── Loop ─────────────────────────────────────────────────────────────────────
unsigned long stallStart  = 0;
unsigned long finishStart = 0;
bool stalledFlag  = false;
bool finishFlag   = false;

void loop() {
  calcRPM();

  if (state == IDLE || state == GRINDING) {
    if (btnPressed(btnPlus) && targetRPM < TARGET_RPM_MAX) {
      targetRPM += 5;
      pidSetpoint = targetRPM;
    }
    if (btnPressed(btnMinus) && targetRPM > TARGET_RPM_MIN) {
      targetRPM -= 5;
      pidSetpoint = targetRPM;
    }
  }

  if (btnPressed(btnStart)) {
    if (state == IDLE || state == DONE) {
      stalledFlag = false;
      finishFlag  = false;
      stallStart  = 0;
      finishStart = 0;
      rpm = 0;
      pulseCount = 0;
      lastRpmCalc = millis();
      pidSetpoint = targetRPM;
      grindPID.SetMode(AUTOMATIC);
      rampTo(80);
      state = GRINDING;
    } else {
      rampTo(0);
      stopMotor();
      state = IDLE;
      drawDisplay("STOPPED");
      delay(800);
    }
  }

  switch (state) {

    case IDLE:
      drawDisplay("IDLE");
      break;

    case GRINDING: {
      pidInput = rpm;
      grindPID.Compute();
      setMotor((int)pidOutput);

      drawDisplay("GRINDING");
      Serial.print("RPM: "); Serial.print(rpm);
      Serial.print("  PWM: "); Serial.println(currentPWM);

      if (rpm < STALL_RPM && currentPWM > 80) {
        if (!stalledFlag) { stalledFlag = true; stallStart = millis(); }
        if (millis() - stallStart > STALL_DELAY_MS) {
          state = STALL_RECOVERY;
          stalledFlag = false;
        }
      } else {
        stalledFlag = false;
      }

      if (rpm > pidSetpoint + FINISH_RPM_DELTA && currentPWM > 80) {
        if (!finishFlag) { finishFlag = true; finishStart = millis(); }
        if (millis() - finishStart > FINISH_HOLD_MS) {
          rampTo(0);
          stopMotor();
          state = DONE;
        }
      } else {
        finishFlag = false;
      }
      break;
    }

    case STALL_RECOVERY:
      drawDisplay("JAM! REV");
      reverseBlip();
      rpm = 0; pulseCount = 0; lastRpmCalc = millis();
      rampTo(80);
      state = GRINDING;
      break;

    case DONE:
      drawDisplay("DONE!");
      for (int i = 0; i < 4; i++) {
        display.invertDisplay(true);  delay(200);
        display.invertDisplay(false); delay(200);
      }
      state = IDLE;
      break;
  }
}
