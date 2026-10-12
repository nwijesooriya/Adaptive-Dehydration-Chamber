#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Keypad.h>

// New 1.3-inch OLED: yellow SDA -> GPIO8, green SCL -> GPIO9
Adafruit_SH1106G display(128, 64, &Wire, -1);

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// Keypad connector pins 1–8, in the order we wired them
byte rowPins[ROWS] = {38, 39, 40, 41};
byte colPins[COLS] = {42, 47, 2, 1};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);  // SDA, SCL

  if (!display.begin(0x3C, true)) {
    Serial.println("OLED not found");
    while (true) delay(1000);
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 4);
  display.println("KEYPAD TEST");
  display.setCursor(0, 25);
  display.println("Press a key...");
  display.display();

  Serial.println("Keypad ready. Press each key.");
}

void loop() {
  char key = keypad.getKey();

  if (key) {
    Serial.print("Pressed: ");
    Serial.println(key);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 4);
    display.println("KEYPAD TEST");

    display.setTextSize(2);
    display.setCursor(0, 27);
    display.print("Key: ");
    display.println(key);
    display.display();
  }
}