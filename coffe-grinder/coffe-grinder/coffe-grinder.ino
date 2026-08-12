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

Adafruit_SSD1306 display(128, 64, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

ButtonConfig cfgStart;
AceButton btnStart(&cfgStart, BTN_START);
AceButton btnPlus(BTN_PLUS);
AceButton btnMinus(BTN_MINUS);

int value = 0;
bool dirty = true;

unsigned long flashUntil = 0;
int flashPin = LED_START;

void flash(int pin) {
  flashPin   = pin;
  flashUntil = millis() + 150;
}

void handleEvent(AceButton* b, uint8_t event, uint8_t state) {
  bool fire = (event == AceButton::kEventPressed ||
               event == AceButton::kEventRepeatPressed);

  switch (b->getPin()) {
    case BTN_PLUS:
      if (fire) { value++; flash(LED_PLUSMIN); dirty = true; }
      break;
    case BTN_MINUS:
      if (fire) { value--; flash(LED_PLUSMIN); dirty = true; }
      break;
    case BTN_START:
      if (event == AceButton::kEventReleased) {
        value = 0;
        flash(LED_START);
        dirty = true;
      }
      break;
  }
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

void setup() {
  pinMode(BTN_START,   INPUT_PULLUP);
  pinMode(BTN_PLUS,    INPUT_PULLUP);
  pinMode(BTN_MINUS,   INPUT_PULLUP);
  pinMode(LED_START,   OUTPUT);
  pinMode(LED_PLUSMIN, OUTPUT);

  ButtonConfig* cfg = ButtonConfig::getSystemButtonConfig();
  cfg->setEventHandler(handleEvent);
  cfg->setFeature(ButtonConfig::kFeatureRepeatPress);
  cfg->setRepeatPressDelay(400);
  cfg->setRepeatPressInterval(120);

  cfgStart.setEventHandler(handleEvent);
  cfgStart.setFeature(ButtonConfig::kFeatureLongPress);
  cfgStart.setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);

  display.begin(SSD1306_SWITCHCAPVCC);
}

void loop() {
  btnStart.check();
  btnPlus.check();
  btnMinus.check();

  if (dirty) {
    draw();
    dirty = false;
  }

  if (millis() < flashUntil) {
    analogWrite(flashPin, 255);
    analogWrite(flashPin == LED_START ? LED_PLUSMIN : LED_START, 40);
  } else {
    analogWrite(LED_START,   40);
    analogWrite(LED_PLUSMIN, 40);
  }
}
