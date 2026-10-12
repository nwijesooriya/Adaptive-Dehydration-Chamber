#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ESP32-S3 pins
#define SDA_PIN 8
#define SCL_PIN 9
#define MOISTURE_PIN 4

// Temporary calibration values
#define DRY_VALUE 2665
#define WET_VALUE 1200

void setup() {
  Serial.begin(115200);

  // I2C for OLED
  Wire.begin(SDA_PIN, SCL_PIN);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED initialization failed!");
    while (true);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("Moisture Test");
  display.println("Starting...");
  display.display();

  delay(1500);
}

void loop() {

  int rawValue = analogRead(MOISTURE_PIN);

  // Convert raw ADC to relative percentage
  float moisturePercent =
      ((float)(DRY_VALUE - rawValue) /
       (DRY_VALUE - WET_VALUE)) * 100.0;

  // Prevent values below 0 or above 100
  moisturePercent = constrain(moisturePercent, 0.0, 100.0);

  // Serial Monitor
  Serial.print("Raw ADC: ");
  Serial.print(rawValue);

  Serial.print(" | Relative Moisture: ");
  Serial.print(moisturePercent, 1);
  Serial.println("%");

  // OLED
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("MOISTURE TEST");
  display.println("----------------");

  display.print("Raw: ");
  display.println(rawValue);

  display.print("Relative: ");
  display.print(moisturePercent, 1);
  display.println("%");

  display.println();
  display.println("NOT calibrated MC%");

  display.display();

  delay(1000);
}