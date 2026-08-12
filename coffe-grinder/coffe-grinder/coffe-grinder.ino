#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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

Adafruit_SSD1306 display(128, 64, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

int value = 0;

struct Button {
  uint8_t pin;
  bool    last;
  unsigned long lastMs;
};

Button bStart = {BTN_START, HIGH, 0};
Button bPlus  = {BTN_PLUS,  HIGH, 0};
Button bMinus = {BTN_MINUS, HIGH, 0};

bool pressed(Button &b) {
  bool now = digitalRead(b.pin);
  bool hit = (now == LOW && b.last == HIGH && millis() - b.lastMs > 50);
  if (hit) b.lastMs = millis();
  b.last = now;
  return hit;
}

void draw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 4);
  display.print("VALUE");

  display.setTextSize(4);
  display.setCursor(0, 24);
  display.print(value);

  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("START = reset");

  display.display();
}

unsigned long flashUntil = 0;
int flashPin = LED_START;

void flash(int pin) {
  flashPin   = pin;
  flashUntil = millis() + 150;
}

void setup() {
  pinMode(BTN_START,   INPUT_PULLUP);
  pinMode(BTN_PLUS,    INPUT_PULLUP);
  pinMode(BTN_MINUS,   INPUT_PULLUP);
  pinMode(LED_START,   OUTPUT);
  pinMode(LED_PLUSMIN, OUTPUT);

  display.begin(SSD1306_SWITCHCAPVCC);
  draw();
}

void loop() {
  bool changed = false;

  if (pressed(bPlus))  { value++;   flash(LED_PLUSMIN); changed = true; }
  if (pressed(bMinus)) { value--;   flash(LED_PLUSMIN); changed = true; }
  if (pressed(bStart)) { value = 0; flash(LED_START);   changed = true; }

  if (changed) draw();

  if (millis() < flashUntil) {
    analogWrite(flashPin, 255);
    analogWrite(flashPin == LED_START ? LED_PLUSMIN : LED_START, 40);
  } else {
    analogWrite(LED_START,   40);
    analogWrite(LED_PLUSMIN, 40);
  }

  delay(10);
}
