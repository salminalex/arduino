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

volatile long pulses = 0;
unsigned long lastTime = 0;
float rpm = 0;

Adafruit_SSD1306 display(128, 64, &SPI, OLED_DC, OLED_RESET, OLED_CS);

void countPulse() { pulses++; }

void showStatus(String status, float rpm, int pwm) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(status);
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print((int)rpm);
  display.println(" RPM");
  display.setTextSize(1);
  display.setCursor(0, 52);
  display.print("PWM: ");
  display.println(pwm);
  display.display();
}

void calcRPM() {
  unsigned long now = millis();
  if (now - lastTime >= 300) {
    detachInterrupt(digitalPinToInterrupt(ENCODER_A));
    rpm = (pulses / 16.0) / ((now - lastTime) / 60000.0)  / 51.0;
    pulses = 0;
    lastTime = now;
    attachInterrupt(digitalPinToInterrupt(ENCODER_A), countPulse, RISING);
  }
}

void rampUp(int pin, int maxPWM) {
  for (int i = 0; i <= maxPWM; i += 3) {
    analogWrite(pin, i);
    calcRPM();
    showStatus("ACCEL", rpm, i);
    delay(20);
  }
}

void rampDown(int pin, int maxPWM) {
  for (int i = maxPWM; i >= 0; i -= 3) {
    analogWrite(pin, i);
    calcRPM();
    showStatus("BRAKE", rpm, i);
    delay(20);
  }
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

  rampUp(RPWM, 255);
  for (int i = 0; i < 20; i++) {
    calcRPM();
    showStatus("FORWARD", rpm, 200);
    Serial.print("RPM: ");
    Serial.println(rpm);
    delay(100);
  }

  rampDown(RPWM, 200);
  showStatus("STOP", 0, 0);
  delay(1000);

  rampUp(LPWM, 200);
  for (int i = 0; i < 20; i++) {
    calcRPM();
    showStatus("REVERSE", rpm, 200);
    Serial.print("RPM: ");
    Serial.println(rpm);
    delay(100);
  }

  rampDown(LPWM, 200);
  showStatus("STOP", 0, 0);
}

void loop() {}