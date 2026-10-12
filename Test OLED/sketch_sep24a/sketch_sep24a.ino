#include <Wire.h>

#define SDA_PIN 8
#define SCL_PIN 9

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.println();
  Serial.println("I2C Scanner Starting...");
}

void loop() {
  byte error;
  int deviceCount = 0;

  Serial.println("Scanning...");

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Device found at 0x");

      if (address < 16) {
        Serial.print("0");
      }

      Serial.println(address, HEX);
      deviceCount++;
    }
  }

  if (deviceCount == 0) {
    Serial.println("No I2C devices found!");
  } else {
    Serial.print("Total devices: ");
    Serial.println(deviceCount);
  }

  Serial.println("--------------------");
  delay(3000);
}