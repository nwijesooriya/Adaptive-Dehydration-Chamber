#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Adafruit_SHT31 sht;

const int SOIL_PIN = 4;
bool oledOK = false;
bool shtOK = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(8, 9);          // SDA, SCL
  analogReadResolution(12); // Soil reading: 0 to 4095

  oledOK = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  shtOK = sht.begin(0x44);

  Serial.println("Component test started");
  Serial.println(oledOK ? "OLED: found" : "OLED: NOT FOUND");
  Serial.println(shtOK ? "SHT31: found" : "SHT31: NOT FOUND");
}

void loop() {
  // Retry if the SHT31 was missing at startup.
  if (!shtOK) shtOK = sht.begin(0x44);

  float temp = shtOK ? sht.readTemperature() : NAN;
  float humidity = shtOK ? sht.readHumidity() : NAN;

  int total = 0;
  for (int i = 0; i < 8; i++) {
    total += analogRead(SOIL_PIN);
    delay(10);
  }
  int soilRaw = total / 8;

  Serial.print("Temp: ");
  if (isnan(temp)) Serial.print("ERROR");
  else Serial.print(temp, 1);

  Serial.print(" C | Humidity: ");
  if (isnan(humidity)) Serial.print("ERROR");
  else Serial.print(humidity, 1);

  Serial.print(" % | Soil raw: ");
  Serial.println(soilRaw);

  if (oledOK) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    oled.setCursor(0, 0);
    oled.println("COMPONENT TEST");
    oled.drawLine(0, 11, 127, 11, SSD1306_WHITE);

    oled.setCursor(0, 17);
    oled.print("Temp: ");
    if (isnan(temp)) oled.println("ERROR");
    else {
      oled.print(temp, 1);
      oled.println(" C");
    }

    oled.setCursor(0, 29);
    oled.print("Humidity: ");
    if (isnan(humidity)) oled.println("ERROR");
    else {
      oled.print(humidity, 1);
      oled.println(" %");
    }

    oled.setCursor(0, 41);
    oled.print("Soil raw: ");
    oled.println(soilRaw);

    oled.setCursor(0, 53);
    oled.print("SHT: ");
    oled.print((!isnan(temp) && !isnan(humidity)) ? "OK" : "ERROR");
    oled.println("  OLED: OK");
    oled.display();
  }

  delay(1000);
}