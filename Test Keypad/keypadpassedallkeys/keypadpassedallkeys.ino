// 2-Channel Relay Test - ESP32-S3

#define RELAY1 10
#define RELAY2 11

// Most boards of this type are active LOW:
// LOW  = relay ON
// HIGH = relay OFF

void setup() {
  Serial.begin(115200);

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);

  // Start safely with both relays OFF
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);

  delay(2000);
  Serial.println("Relay test starting...");
}

void loop() {

  Serial.println("CH1 ON");
  digitalWrite(RELAY1, LOW);
  delay(2000);

  Serial.println("CH1 OFF");
  digitalWrite(RELAY1, HIGH);
  delay(2000);

  Serial.println("CH2 ON");
  digitalWrite(RELAY2, LOW);
  delay(2000);

  Serial.println("CH2 OFF");
  digitalWrite(RELAY2, HIGH);
  delay(2000);

  Serial.println("BOTH ON");
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);
  delay(2000);

  Serial.println("BOTH OFF");
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  delay(3000);
}