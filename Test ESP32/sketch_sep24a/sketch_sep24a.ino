#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SDA_PIN 8
#define SCL_PIN 9

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED initialization failed!");
    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Row 1
  display.setCursor(0, 0);
  display.println("DEHYDRATION MODULE");

  // Separator line
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Row 2
  display.setCursor(0, 13);
  display.println("M:33%  Target:23%");

  // Row 3
  display.setCursor(0, 23);
  display.println("Temp:27.1C  RH:30%");

  // Row 4
  display.setCursor(0, 33);
  display.println("Fan:76%  Heat:ON");

  // Row 5
  display.setCursor(0, 43);
  display.println("Weight:2.45 kg");

  // Row 6
  display.setCursor(0, 53);
  display.println("Time:01:35:20");

  display.display();

  Serial.println("Dashboard displayed!");
}

void loop() {
}