#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_CS    8
#define OLED_DC    9
#define OLED_RESET 10

#define RPWM 5
#define LPWM 6
#define R_EN 4
#define L_EN 7

#define ENCODER_A 2
#define ENCODER_B 3

#define GEAR_RATIO     51.0
#define PULSES_PER_REV 16.0

volatile long pulses = 0;
unsigned long lastTime = 0;
float rpm = 0;
int currentPWM = 0;
bool running = false;

Adafruit_SSD1306 display(128, 64, &SPI, OLED_DC, OLED_RESET, OLED_CS);

void countPulse() { pulses++; }

void showStatus(const char* status, float rpm, int pwm) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Yellow zone: status
  display.setTextSize(1);
  display.setCursor(0, 4);
  display.print(status);

  // Blue zone: RPM big
  display.setTextSize(3);
  display.setCursor(0, 18);
  display.print((int)rpm);

  // PWM bottom
  display.setTextSize(1);
  display.setCursor(0, 52);
  display.print("PWM: ");
  display.print(pwm);

  display.display();
}

void calcRPM() {
  unsigned long now = millis();
  if (now - lastTime >= 300) {
    noInterrupts();
    long p = pulses;
    pulses = 0;
    interrupts();
    rpm = (p / PULSES_PER_REV) / ((now - lastTime) / 60000.0) / GEAR_RATIO;
    lastTime = now;
  }
}

void rampTo(int pin, int from, int to, const char* label) {
  int step = (to > from) ? 3 : -3;
  for (int i = from; (step > 0) ? (i <= to) : (i >= to); i += step) {
    analogWrite(pin, i);
    currentPWM = i;
    calcRPM();
    showStatus(label, rpm, i);
    delay(20);
  }
  currentPWM = to;
}

void setup() {
  Serial.begin(9600);

  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);
  pinMode(ENCODER_A, INPUT_PULLUP);
  pinMode(ENCODER_B, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENCODER_A), countPulse, RISING);

  display.begin(SSD1306_SWITCHCAPVCC);
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);
  lastTime = millis();

  showStatus("READY", 0, 0);
}

void loop() {
  // Forward
  rampTo(RPWM, 0, 255, "ACCEL");
  for (int i = 0; i < 20; i++) {
    calcRPM();
    showStatus("FORWARD", rpm, currentPWM);
    Serial.print("RPM: ");
    Serial.println(rpm);
    delay(100);
  }
  rampTo(RPWM, 255, 0, "BRAKE");
  showStatus("STOP", 0, 0);
  delay(1000);

  // Reverse
  rampTo(LPWM, 0, 255, "ACCEL");
  for (int i = 0; i < 20; i++) {
    calcRPM();
    showStatus("REVERSE", rpm, currentPWM);
    Serial.print("RPM: ");
    Serial.println(rpm);
    delay(100);
  }
  rampTo(LPWM, 255, 0, "BRAKE");
  showStatus("STOP", 0, 0);
  delay(1000);
}
