#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ------------------------------
// WIFI SETTINGS
// ------------------------------
const char* WIFI_SSID = "Dialog 4G 256";
const char* WIFI_PASSWORD = "ADEfbF0D";

// IMPORTANT:
// Replace this with your LAPTOP'S IPv4 address.
// Do NOT use 127.0.0.1.
const char* RDT_API =
  "http://192.168.8.129:5001/predict-rdt";


void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=== RDT API TEST ===");

  // ------------------------------
  // CONNECT TO WIFI
  // ------------------------------
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");

  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  // Wait briefly before sending request
  delay(1000);

  testRDTPrediction();
}


void loop() {
  // Nothing needed for this first test.
}


// ======================================================
// SEND TEST DATA TO RDT MODEL
// ======================================================
void testRDTPrediction() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected.");
    return;
  }

  HTTPClient http;

  http.begin(RDT_API);

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  // These are TEST values only.
  // Later they will come from your actual chamber.
  String jsonData = R"({
    "initialMoisturePct": 30,
    "targetMoisturePct": 14,
    "elapsedMin": 120,
    "temperatureC": 39,
    "humidityPct": 51,
    "currentMoisturePct": 20
  })";

  Serial.println();
  Serial.println("Sending drying data...");
  Serial.println(jsonData);

  int httpCode = http.POST(jsonData);

  Serial.print("HTTP response: ");
  Serial.println(httpCode);

  if (httpCode > 0) {

    String response = http.getString();

    Serial.println("Model response:");
    Serial.println(response);

    // ------------------------------
    // PARSE JSON RESPONSE
    // ------------------------------
    JsonDocument doc;

    DeserializationError error =
      deserializeJson(doc, response);

    if (!error) {

      float rdt =
        doc["remainingDryingTimeMin"];

      Serial.println();
      Serial.print("Predicted RDT = ");
      Serial.print(rdt);
      Serial.println(" minutes");

      int hours = (int)rdt / 60;
      int minutes = (int)rdt % 60;

      Serial.print("RDT = ");
      Serial.print(hours);
      Serial.print("h ");
      Serial.print(minutes);
      Serial.println("m");

    } else {

      Serial.print("JSON error: ");
      Serial.println(error.c_str());
    }

  } else {

    Serial.print("HTTP request failed: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}