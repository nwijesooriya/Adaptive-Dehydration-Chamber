#include "HX711.h"

#define HX711_DT  16
#define HX711_SCK 15

HX711 scale;

float calibrationFactor = 1.0;
const float referenceWeight = 100.0; // 100 g soap

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("HX711 LOAD CELL CALIBRATION");
  Serial.println("===========================");

  scale.begin(HX711_DT, HX711_SCK);

  if (!scale.is_ready()) {
    Serial.println("ERROR: HX711 not detected!");
    return;
  }

  Serial.println("HX711 detected.");
  Serial.println();
  Serial.println("COMMANDS:");
  Serial.println("T = Tare empty tray");
  Serial.println("C = Calibrate using 100 g soap");
  Serial.println();

  // Start with raw scale factor
  scale.set_scale(1.0);
}

void loop() {

  // ------------------------------------------------
  // SERIAL COMMANDS
  // ------------------------------------------------

  if (Serial.available()) {

    char command = Serial.read();

    if (command == 'T' || command == 't') {

      Serial.println();
      Serial.println("TARING...");
      Serial.println("Make sure the tray is EMPTY.");

      delay(2000);

      scale.tare(20);

      Serial.println("TARE COMPLETE.");
      Serial.println();
    }

    else if (command == 'C' || command == 'c') {

      Serial.println();
      Serial.println("CALIBRATION STARTED");
      Serial.println("Place the 100 g soap in the CENTER.");
      Serial.println("Do not touch the tray.");
      Serial.println("Waiting 5 seconds...");

      delay(5000);

      scale.set_scale(1.0);

      float rawValue = scale.get_units(30);

      calibrationFactor = rawValue / referenceWeight;

      scale.set_scale(calibrationFactor);

      Serial.println();
      Serial.print("New calibration factor = ");
      Serial.println(calibrationFactor, 6);

      delay(1000);

      float verifiedWeight = scale.get_units(20);

      Serial.print("Measured weight after calibration = ");
      Serial.print(verifiedWeight, 2);
      Serial.println(" g");

      Serial.println();
      Serial.println("COPY THIS CALIBRATION FACTOR.");
      Serial.println();
    }
  }

  // ------------------------------------------------
  // LIVE WEIGHT
  // ------------------------------------------------

  if (scale.is_ready()) {

    float weight = scale.get_units(10);

    if (weight > -0.5 && weight < 0.5) {
      weight = 0;
    }

    Serial.print("Weight: ");
    Serial.print(weight, 2);
    Serial.println(" g");
  }

  delay(1000);
}