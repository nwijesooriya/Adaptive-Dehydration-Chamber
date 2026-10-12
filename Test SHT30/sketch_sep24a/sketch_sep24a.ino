#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>

#define SDA_PIN 8
#define SCL_PIN 9

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define OLED_ADDRESS 0x3C
#define SHT_ADDRESS  0x44

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

Adafruit_SHT31 sht31 = Adafruit_SHT31();

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  // Start OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED initialization failed!");
    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Start SHT3X
  if (!sht31.begin(SHT_ADDRESS)) {
    Serial.println("SHT3X sensor not found!");

    display.setTextSize(1);
    display.setCursor(0, 10);
    display.println("SHT3X NOT FOUND!");
    display.println("Check the wiring.");
    display.display();

    while (true) {
      delay(1000);
    }
  }

  Serial.println("OLED and SHT3X started!");
}

void loop() {
  float temperature = sht31.readTemperature();
  float humidity = sht31.readHumidity();

  display.clearDisplay();

  // Heading
  display.setTextSize(1);
  display.setCursor(7, 0);
  display.println("DEHYDRATION MODULE");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  if (!isnan(temperature) && !isnan(humidity)) {
    // Temperature
    display.setTextSize(2);
    display.setCursor(0, 17);
    display.print("T: ");
    display.print(temperature, 1);
    display.println(" C");

    // Humidity
    display.setCursor(0, 42);
    display.print("H: ");
    display.print(humidity, 1);
    display.println(" %");

    Serial.print("Temperature: ");
    Serial.print(temperature, 1);
    Serial.print(" C | Humidity: ");
    Serial.print(humidity, 1);
    Serial.println(" %");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.println("Sensor reading");
    display.println("failed!");

    Serial.println("Failed to read SHT3X!");
  }

  display.display();
  delay(2000);
}