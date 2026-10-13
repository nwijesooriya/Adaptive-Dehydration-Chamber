#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <HX711.h>
#include <Keypad.h>
#include <Preferences.h>
#include <math.h>

// Your existing connections
const int HX_DT  = 6;  // HX711 DT: yellow
const int HX_SCK = 5;  // HX711 SCK: green
const float REFERENCE_G = 100.0f;

Adafruit_SH1106G display(128, 64, &Wire, -1);
HX711 scale;
Preferences preferences;

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {38, 39, 40, 41};
byte colPins[COLS] = {42, 47, 2, 1};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

bool oledOK = false;
bool tared = false;
bool calibrated = false;
float factor = 0.0f;
unsigned long lastReading = 0;

void tareEmptyPlate() {
  if (!scale.is_ready()) {
    Serial.println("HX711 not ready. Cannot tare.");
    return;
  }

  Serial.println("Taring EMPTY plate...");
  scale.tare(10);
  tared = true;
  Serial.println("Tare complete.");
}

void calibrateWith100g() {
  if (!scale.is_ready()) {
    Serial.println("HX711 not ready. Cannot calibrate.");
    return;
  }

  if (!tared) {
    Serial.println("Tare the empty plate first: press *");
    return;
  }

  // get_value() is the average raw reading minus the tare offset.
  float rawChange = scale.get_value(10);
  Serial.print("Raw change with 100 g: ");
  Serial.println(rawChange, 1);

  if (fabsf(rawChange) < 10.0f) {
    Serial.println("Calibration failed: load cell barely changed.");
    return;
  }

  factor = rawChange / REFERENCE_G;
  scale.set_scale(factor);
  calibrated = true;
  preferences.putFloat("factor", factor);

  Serial.print("New 2 kg cell calibrated. Factor: ");
  Serial.println(factor, 6);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);  // OLED SDA GPIO8, SCL GPIO9

  oledOK = display.begin(0x3C, true);
  if (!oledOK) Serial.println("OLED not found; Serial Monitor will still work.");

  scale.begin(HX_DT, HX_SCK);

  preferences.begin("scale2kg", false);
  factor = preferences.getFloat("factor", 0.0f);
  calibrated = fabsf(factor) > 0.000001f;

  if (calibrated) {
    scale.set_scale(factor);
    Serial.println("Saved 2 kg calibration loaded.");
  } else {
    Serial.println("No 2 kg calibration yet.");
  }

  Serial.println("Keep the plate EMPTY during startup.");
  if (scale.is_ready()) {
    tareEmptyPlate();
  } else {
    Serial.println("HX711 not ready. Check its connections.");
  }

  Serial.println("* = tare EMPTY plate");
  Serial.println("# = calibrate with ONE 100 g soap");
}

void loop() {
  char key = keypad.getKey();

  if (key == '*') tareEmptyPlate();
  if (key == '#') calibrateWith100g();

  if (millis() - lastReading < 1000) return;
  lastReading = millis();

  bool ready = scale.is_ready();
  long rawChange = 0;
  float grams = 0;

  if (ready && tared) {
    rawChange = scale.get_value(5);
    if (calibrated) {
      grams = rawChange / factor;
    }
  }

  Serial.print("HX711: ");
  Serial.print(ready ? "READY" : "NOT READY");
  Serial.print(" | Raw change: ");

  if (ready && tared) Serial.print(rawChange);
  else Serial.print("--");

  Serial.print(" | Weight: ");
  if (ready && tared && calibrated) {
    Serial.print(grams, 1);
    Serial.println(" g");
  } else {
    Serial.println("--");
  }

  if (oledOK) {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("NEW 2KG LOAD CELL");

    display.setCursor(0, 13);
    display.print("HX: ");
    display.println(ready ? "READY" : "NOT READY");

    display.setCursor(0, 25);
    display.print("Raw: ");
    if (ready && tared) display.println(rawChange);
    else display.println("--");

    display.setCursor(0, 37);
    display.print("Weight: ");
    if (ready && tared && calibrated) {
      display.print(grams, 1);
      display.println(" g");
    } else {
      display.println("CALIBRATE");
    }

    display.setCursor(0, 54);
    display.print("* tare  # 100g");
    display.display();
  }
}