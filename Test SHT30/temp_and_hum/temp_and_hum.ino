#include <HX711.h>

HX711 scale;

void setup() {
  Serial.begin(115200);
  delay(1500);
  scale.begin(6, 5); // DT = GPIO6 (yellow), SCK = GPIO5 (green)
}

void loop() {
  if (scale.wait_ready_timeout(1000)) {
    Serial.print("ADC raw: ");
    Serial.println(scale.read());
  } else {
    Serial.println("HX711 not responding");
  }
  delay(500);
}