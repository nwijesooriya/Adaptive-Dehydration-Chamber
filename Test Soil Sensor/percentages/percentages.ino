#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HX711.h>
#include <Preferences.h>

const int SDA_PIN = 8;
const int SCL_PIN = 9;
const int HX_DT = 6;    // Yellow wire
const int HX_SCK = 5;   // Green wire

const float KNOWN_WEIGHT_G = 100.0; // Change to your known object's weight

Adafruit_SSD1306 display(128, 64, &Wire, -1);
HX711 scale;
Preferences settings;

float calibrationFactor = 0;
bool calibrated = false;

void showMessage(const char *line1, const char *line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 12);
  display.println(line1);
  display.setCursor(0, 30);
  display.println(line2);
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found");
    while (true) delay(1000);
  }

  showMessage("Load cell starting", "Keep plate EMPTY");

  scale.begin(HX_DT, HX_SCK);

  if (!scale.wait_ready_timeout(3000)) {
    showMessage("HX711 error", "Check wiring");
    Serial.println("HX711 not responding. Check wiring.");
    while (true) delay(1000);
  }

  settings.begin("cinnamon-scale", false);
  calibrationFactor = settings.getFloat("factor", 0);
  calibrated = (calibrationFactor != 0);

  if (calibrated) {
    scale.set_scale(calibrationFactor);
  }

  scale.tare(15); // Empty plate becomes zero grams

  Serial.println("Ready.");
  Serial.println("Type t to tare with the plate empty.");
  Serial.println("Put the known weight on the plate, then type c to calibrate.");
}

void loop() {
  if (Serial.available()) {
    char command = Serial.read();

    if (command == 't' || command == 'T') {
      if (scale.wait_ready_timeout(1000)) {
        scale.tare(15);
        Serial.println("Tared: current load is now zero.");
      }
    }

    if (command == 'c' || command == 'C') {
      if (scale.wait_ready_timeout(1000)) {
        // Raw difference between the known weight and the empty plate
        float difference = scale.get_value(15);

        if (abs(difference) < 1) {
          Serial.println("No weight change detected. Calibration cancelled.");
        } else {
          calibrationFactor = difference / KNOWN_WEIGHT_G;
          scale.set_scale(calibrationFactor);
          settings.putFloat("factor", calibrationFactor);
          calibrated = true;

          Serial.print("Calibrated. Factor: ");
          Serial.println(calibrationFactor, 4);
        }
      }
    }
  }

  if (!scale.wait_ready_timeout(1000)) {
    showMessage("HX711 error", "No reading");
    delay(500);
    return;
  }

  if (!calibrated) {
    long rawChange = scale.get_value(5);
    showMessage("Not calibrated", "Use Serial: t, c");
    Serial.print("Raw change: ");
    Serial.println(rawChange);
  } else {
    float grams = scale.get_units(5);

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 2);
    display.println("CINNAMON SCALE");

    display.setTextSize(2);
    display.setCursor(0, 24);
    display.print(grams, 1);
    display.print(" g");

    display.setTextSize(1);
    display.setCursor(0, 55);
    display.print("t=tare  c=calibrate");
    display.display();

    Serial.print("Weight: ");
    Serial.print(grams, 1);
    Serial.println(" g");
  }

  delay(300);
}