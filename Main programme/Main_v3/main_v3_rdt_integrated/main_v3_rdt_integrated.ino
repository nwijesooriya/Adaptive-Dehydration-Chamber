#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_SHT31.h>
#include <HX711.h>
#include <Keypad.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// =============================================================
// SMART CINNAMON DEHYDRATION CHAMBER
// ESP32-S3 DevKitC-1 N16R8
//
// FINAL PP1 DRYING FIRMWARE - V3 + RDT API
//
// CURRENT GPIO MAP
//
// SHT30:
//   SDA -> GPIO 6
//   SCL -> GPIO 5
//
// OLED:
//   SDA -> GPIO 8
//   SCL -> GPIO 9
//
// HX711:
//   DT  -> GPIO 16
//   SCK -> GPIO 15
//
// RELAYS:
//   Heater -> GPIO 11
//   Fan    -> GPIO 10
//
// KEYPAD ROWS:
//   38, 39, 40, 41
//
// KEYPAD COLS:
//   42, 47, 2, 1
//
// Relay board = ACTIVE LOW
// =============================================================


// =============================================================
// PIN DEFINITIONS
// =============================================================

// SHT30
#define SHT_SDA 6
#define SHT_SCL 5

// OLED
#define OLED_SDA 8
#define OLED_SCL 9

// HX711
#define HX711_DT  16
#define HX711_SCK 15

// Relays
#define HEATER_RELAY_PIN 11
#define FAN_RELAY_PIN    10

#define RELAY_ON  LOW
#define RELAY_OFF HIGH


// =============================================================
// SETTINGS
// =============================================================

// Calibration reference
float calibrationReferenceWeight = 0.0;

// Cooling / purge time after target MC reached
const unsigned long COOLING_TIME_MS = 120000UL; // 2 minutes

// Sensor update
const unsigned long SENSOR_INTERVAL_MS = 1000UL;

// OLED refresh
const unsigned long DISPLAY_INTERVAL_MS = 500UL;

// Research-data logging interval
const unsigned long LOG_INTERVAL_MS = 60000UL; // 1 minute

// Upload live telemetry to Express/MongoDB every 60 seconds
const unsigned long TELEMETRY_UPLOAD_INTERVAL_MS = 60000UL;

// -------------------------------------------------------------
// WI-FI + RDT MODEL API
// -------------------------------------------------------------
// CHANGE ONLY THESE THREE VALUES before uploading.
// Keep the Flask model server running on the laptop.
// The ESP32 and laptop must be connected to the same Wi-Fi.
// -------------------------------------------------------------

const char* WIFI_SSID = "Dialog 4G 256";
const char* WIFI_PASSWORD = "ADEfbF0D";

// Replace 192.168.8.XXX with the laptop IPv4 address that worked
// when you opened the Flask API from the browser.
const char* RDT_API =
  "http://192.168.8.129:5001/predict-rdt";

// Express / MongoDB telemetry endpoint
const char* DASHBOARD_API =
  "http://192.168.8.129:5000/api/dehydration/telemetry";

// First prediction shortly after drying starts, then refresh every 5 min.
const unsigned long RDT_FIRST_REQUEST_DELAY_MS = 60000UL;  // 1 minute
const unsigned long RDT_UPDATE_INTERVAL_MS = 300000UL;     // 5 minutes
const unsigned long RDT_RETRY_INTERVAL_MS = 60000UL;       // retry after 1 minute
const unsigned long WIFI_RETRY_INTERVAL_MS = 30000UL;      // 30 seconds

// -------------------------------------------------------------
// Temperature protection
// -------------------------------------------------------------
// Heater turns OFF at/above this temperature.
// Heater may turn back ON after chamber falls below resume temp.
//
// This is SOFTWARE protection only.
// A physical thermal fuse / thermostat is still recommended.
// -------------------------------------------------------------

const float MAX_CHAMBER_TEMP_C = 50.0;
const float HEATER_RESUME_TEMP_C = 47.0;

// Require several consecutive target readings before stopping.
// Helps prevent one noisy load-cell reading from ending the batch.
const int TARGET_CONFIRM_COUNT = 5;


// =============================================================
// I2C BUSES
// =============================================================

// SHT30 on GPIO 6 / 5
TwoWire SHTBus = TwoWire(0);

// OLED on GPIO 8 / 9
TwoWire OLEDBus = TwoWire(1);


// =============================================================
// DEVICES
// =============================================================

Adafruit_SH1106G display(
  128,
  64,
  &OLEDBus,
  -1
);

Adafruit_SHT31 sht31 =
  Adafruit_SHT31(&SHTBus);

HX711 scale;

Preferences preferences;


// =============================================================
// KEYPAD
// =============================================================

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {
  38, 39, 40, 41
};

byte colPins[COLS] = {
  42, 47, 2, 1
};

Keypad keypad = Keypad(
  makeKeymap(keys),
  rowPins,
  colPins,
  ROWS,
  COLS
);


// =============================================================
// SYSTEM STATES
// =============================================================

enum SystemState {

  STARTUP_TARE,

  ENTER_CAL_WEIGHT,
  CALIBRATE_EMPTY,
  CALIBRATE_WEIGHT,

  ENTER_INITIAL_MC,
  ENTER_TARGET_MC,
  PLACE_CINNAMON,

  READY_TO_START,

  DRYING,
  COOLING,

  COMPLETE,
  ABORTED
};

SystemState systemState = STARTUP_TARE;


// =============================================================
// DISPLAY PAGE
// =============================================================

char activePage = 'A';

// A = Home
// B = Sensors
// C = Control status
// D = Time / RDT


// =============================================================
// SYSTEM VARIABLES
// =============================================================

bool oledOK = false;
bool shtOK = false;
bool hxOK = false;

bool heaterOn = false;
bool fanOn = false;

bool calibrationValid = false;

float calibrationFactor = 1.0;


// -------------------------------------------------------------
// Drying values
// -------------------------------------------------------------

float initialMoisture = 0.0;
float targetMoisture = 0.0;

float initialWeight = 0.0;
float currentWeight = 0.0;

float dryMass = 0.0;
float targetWeight = 0.0;

float estimatedMoisture = NAN;


// -------------------------------------------------------------
// Environmental values
// -------------------------------------------------------------

float chamberTemperature = NAN;
float chamberHumidity = NAN;

unsigned long lastValidSHTReading = 0;


// -------------------------------------------------------------
// Timing
// -------------------------------------------------------------

unsigned long dryingStartMs = 0;
unsigned long dryingDurationMs = 0;

unsigned long coolingStartMs = 0;

unsigned long lastSensorUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastLogTime = 0;
unsigned long lastTelemetryUploadMs = 0;
bool dashboardApiOnline = false;

// -------------------------------------------------------------
// RDT prediction / Wi-Fi state
// -------------------------------------------------------------

float predictedRDTMinutes = -1.0;
bool rdtPredictionValid = false;
bool rdtApiOnline = false;
bool firstRDTPredictionAttempted = false;

unsigned long lastRDTAttemptMs = 0;
unsigned long lastRDTSuccessMs = 0;
unsigned long lastWiFiRetryMs = 0;


// -------------------------------------------------------------
// Target confirmation
// -------------------------------------------------------------

int targetCounter = 0;


// -------------------------------------------------------------
// Input
// -------------------------------------------------------------

String inputBuffer = "";


// -------------------------------------------------------------
// Batch number
// -------------------------------------------------------------

unsigned int batchCounter = 0;
String batchId = "B000";


// =============================================================
// OLED HELPER
// =============================================================

void clearOLED() {

  if (!oledOK)
    return;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
}


// =============================================================
// RELAY CONTROL
// =============================================================

void setHeater(bool on) {

  heaterOn = on;

  digitalWrite(
    HEATER_RELAY_PIN,
    on ? RELAY_ON : RELAY_OFF
  );
}


void setFan(bool on) {

  fanOn = on;

  digitalWrite(
    FAN_RELAY_PIN,
    on ? RELAY_ON : RELAY_OFF
  );
}


void stopEverything() {

  setHeater(false);
  setFan(false);
}


// =============================================================
// TIME FORMAT
// =============================================================

String formatTime(unsigned long milliseconds) {

  unsigned long totalSeconds =
    milliseconds / 1000UL;

  unsigned long hours =
    totalSeconds / 3600UL;

  unsigned long minutes =
    (totalSeconds % 3600UL) / 60UL;

  char buffer[12];

  snprintf(
    buffer,
    sizeof(buffer),
    "%02lu:%02lu",
    hours,
    minutes
  );

  return String(buffer);
}


// =============================================================
// CURRENT DRYING TIME
// =============================================================

unsigned long getDryingElapsed() {

  if (systemState == DRYING) {

    return millis() - dryingStartMs;
  }

  if (
    systemState == COOLING ||
    systemState == COMPLETE ||
    systemState == ABORTED
  ) {

    return dryingDurationMs;
  }

  return 0;
}


// =============================================================
// WI-FI + RDT MODEL HELPERS
// =============================================================

bool wifiConfigured() {

  String ssid = String(WIFI_SSID);
  String api = String(RDT_API);

  if (
    ssid.length() == 0 ||
    ssid == "YOUR_WIFI_NAME" ||
    api.indexOf("XXX") >= 0
  ) {
    return false;
  }

  return true;
}


void connectWiFiAtStartup() {

  if (!wifiConfigured()) {
    Serial.println(
      "RDT WiFi not configured. Drying will continue without ML prediction."
    );
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi for RDT");

  unsigned long startAttempt = millis();

  // Do not allow Wi-Fi to block the chamber forever.
  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startAttempt < 8000UL
  ) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("RDT WiFi connected.");
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
  }
  else {
    Serial.println(
      "RDT WiFi unavailable at startup. Chamber remains operational."
    );
  }
}


void maintainWiFi() {

  if (!wifiConfigured())
    return;

  if (WiFi.status() == WL_CONNECTED)
    return;

  if (
    millis() - lastWiFiRetryMs <
    WIFI_RETRY_INTERVAL_MS
  ) {
    return;
  }

  lastWiFiRetryMs = millis();

  Serial.println("Attempting WiFi reconnect...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}


float getDisplayedRDTMinutes() {

  if (!rdtPredictionValid)
    return -1.0;

  // Let the displayed RDT count down between 5-minute ML refreshes.
  float minutesSincePrediction =
    (millis() - lastRDTSuccessMs) / 60000.0;

  float displayed =
    predictedRDTMinutes - minutesSincePrediction;

  if (displayed < 0.0)
    displayed = 0.0;

  return displayed;
}


bool requestRDTPrediction() {

  lastRDTAttemptMs = millis();

  if (WiFi.status() != WL_CONNECTED) {
    rdtApiOnline = false;
    Serial.println("RDT request skipped: WiFi offline.");
    return false;
  }

  if (
    isnan(chamberTemperature) ||
    isnan(chamberHumidity) ||
    isnan(estimatedMoisture)
  ) {
    Serial.println("RDT request skipped: sensor/moisture input unavailable.");
    return false;
  }

  HTTPClient http;
  http.setTimeout(2500);
  http.begin(RDT_API);
  http.addHeader("Content-Type", "application/json");

  JsonDocument payload;

  payload["initialMoisturePct"] = initialMoisture;
  payload["targetMoisturePct"] = targetMoisture;
  payload["elapsedMin"] = getDryingElapsed() / 60000.0;
  payload["temperatureC"] = chamberTemperature;
  payload["humidityPct"] = chamberHumidity;
  payload["currentMoisturePct"] = estimatedMoisture;

  String jsonData;
  serializeJson(payload, jsonData);

  Serial.println();
  Serial.println("--- RDT MODEL REQUEST ---");
  Serial.println(jsonData);

  int httpCode = http.POST(jsonData);

  Serial.print("RDT HTTP response: ");
  Serial.println(httpCode);

  if (httpCode != 200) {

    if (httpCode < 0) {
      Serial.print("RDT HTTP error: ");
      Serial.println(http.errorToString(httpCode));
    }
    else {
      Serial.println(http.getString());
    }

    rdtApiOnline = false;
    http.end();
    return false;
  }

  String response = http.getString();
  http.end();

  Serial.println("RDT model response:");
  Serial.println(response);

  JsonDocument result;

  DeserializationError error =
    deserializeJson(result, response);

  if (error) {
    Serial.print("RDT JSON parse error: ");
    Serial.println(error.c_str());
    rdtApiOnline = false;
    return false;
  }

  float prediction =
    result["remainingDryingTimeMin"] | -1.0f;

  if (
    prediction < 0.0 ||
    prediction > 1440.0
  ) {
    Serial.println("RDT response rejected: invalid prediction value.");
    rdtApiOnline = false;
    return false;
  }

  predictedRDTMinutes = prediction;
  rdtPredictionValid = true;
  rdtApiOnline = true;
  lastRDTSuccessMs = millis();

  Serial.print("Predicted RDT: ");
  Serial.print(predictedRDTMinutes, 1);
  Serial.println(" min");

  Serial.println("-------------------------");

  return true;
}

// =============================================================
// SEND LIVE TELEMETRY TO EXPRESS / MONGODB
// =============================================================

bool sendTelemetryToDashboard() {

  if (WiFi.status() != WL_CONNECTED) {

    dashboardApiOnline = false;

    Serial.println(
      "Dashboard upload skipped: WiFi offline."
    );

    return false;
  }


  // Only upload meaningful drying-session data
  if (
    systemState != DRYING &&
    systemState != COOLING &&
    systemState != COMPLETE &&
    systemState != ABORTED
  ) {

    return false;
  }


  HTTPClient http;

  http.setTimeout(3000);

  http.begin(DASHBOARD_API);

  http.addHeader(
    "Content-Type",
    "application/json"
  );


  // -----------------------------------------------------------
  // BUILD JSON PAYLOAD
  // -----------------------------------------------------------

  JsonDocument payload;


  payload["batchId"] =
    batchId;


  payload["initialMoisture"] =
    initialMoisture;


  payload["targetMoisture"] =
    targetMoisture;


  payload["initialWeight"] =
    initialWeight;


  payload["currentWeight"] =
    currentWeight;


  payload["targetWeight"] =
    targetWeight;


  if (!isnan(estimatedMoisture)) {

    payload["estimatedMoisture"] =
      estimatedMoisture;
  }


  if (!isnan(chamberTemperature)) {

    payload["temperature"] =
      chamberTemperature;
  }


  if (!isnan(chamberHumidity)) {

    payload["humidity"] =
      chamberHumidity;
  }


  // Backend expects seconds
  payload["elapsedTime"] =
    getDryingElapsed() / 1000UL;


  // -----------------------------------------------------------
  // RDT
  // Dashboard currently stores RDT in seconds
  // -----------------------------------------------------------

  if (rdtPredictionValid) {

    float displayedRDTMinutes =
      getDisplayedRDTMinutes();

    payload["remainingDryingTime"] =
      (unsigned long)
      round(displayedRDTMinutes * 60.0);
  }

  else {

    payload["remainingDryingTime"] =
      nullptr;
  }


  // -----------------------------------------------------------
  // CONTROL STATE
  // -----------------------------------------------------------

  payload["heaterState"] =
    heaterOn ? "ON" : "OFF";


  payload["fanState"] =
    fanOn ? "ON" : "OFF";


  payload["systemState"] =
    stateName();


  // -----------------------------------------------------------
  // ML STATUS
  // -----------------------------------------------------------

  if (rdtApiOnline) {

    payload["mlStatus"] =
      "ONLINE";
  }

  else if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    payload["mlStatus"] =
      "PENDING";
  }

  else {

    payload["mlStatus"] =
      "OFFLINE";
  }


  // -----------------------------------------------------------
  // SERIAL DEBUG
  // -----------------------------------------------------------

  String jsonData;

  serializeJson(
    payload,
    jsonData
  );


  Serial.println();

  Serial.println(
    "--- DASHBOARD TELEMETRY ---"
  );

  Serial.println(
    jsonData
  );


  // -----------------------------------------------------------
  // SEND
  // -----------------------------------------------------------

  int httpCode =
    http.POST(jsonData);


  Serial.print(
    "Dashboard HTTP response: "
  );

  Serial.println(
    httpCode
  );


  if (
    httpCode >= 200 &&
    httpCode < 300
  ) {

    String response =
      http.getString();

    Serial.println(
      "Dashboard telemetry stored."
    );

    Serial.println(
      response
    );


    dashboardApiOnline =
      true;


    http.end();

    return true;
  }


  // -----------------------------------------------------------
  // ERROR
  // -----------------------------------------------------------

  if (httpCode < 0) {

    Serial.print(
      "Dashboard HTTP error: "
    );

    Serial.println(
      http.errorToString(httpCode)
    );
  }

  else {

    Serial.println(
      http.getString()
    );
  }


  dashboardApiOnline =
    false;


  http.end();

  return false;
}

// =============================================================
// DASHBOARD TELEMETRY SCHEDULER
// =============================================================

void updateDashboardTelemetrySchedule() {

  if (
    systemState != DRYING &&
    systemState != COOLING
  ) {

    return;
  }


  if (
    millis() -
    lastTelemetryUploadMs
    < TELEMETRY_UPLOAD_INTERVAL_MS
  ) {

    return;
  }


  lastTelemetryUploadMs =
    millis();


  sendTelemetryToDashboard();
}

void updateRDTPredictionSchedule() {

  if (systemState != DRYING)
    return;

  unsigned long elapsed =
    millis() - dryingStartMs;

  // First request around one minute after START.
  if (!firstRDTPredictionAttempted) {

    if (elapsed >= RDT_FIRST_REQUEST_DELAY_MS) {
      firstRDTPredictionAttempted = true;
      requestRDTPrediction();
    }

    return;
  }

  // If last request succeeded -> normal 5-minute refresh.
  // If it failed -> retry sooner after one minute.
  unsigned long nextInterval =
    rdtApiOnline ?
    RDT_UPDATE_INTERVAL_MS :
    RDT_RETRY_INTERVAL_MS;

  if (
    millis() - lastRDTAttemptMs >=
    nextInterval
  ) {
    requestRDTPrediction();
  }
}


// =============================================================
// MOISTURE ESTIMATION
// =============================================================
//
// Wet-basis mass balance:
//
// DryMass = InitialWeight * (1 - InitialMC/100)
//
// CurrentMC =
// ((CurrentWeight - DryMass) / CurrentWeight) * 100
//
// =============================================================

float calculateEstimatedMoisture() {

  if (
    initialWeight <= 0 ||
    currentWeight <= 0 ||
    dryMass <= 0
  ) {

    return NAN;
  }

  float mc =
    ((currentWeight - dryMass) /
     currentWeight) * 100.0;

  if (mc < 0)
    mc = 0;

  if (mc > 100)
    mc = 100;

  return mc;
}


// =============================================================
// SENSOR UPDATE
// =============================================================

void updateSensors() {

  // -----------------------------------------------------------
  // SHT30
  // -----------------------------------------------------------

  float temp =
    sht31.readTemperature();

  float hum =
    sht31.readHumidity();

  if (
    !isnan(temp) &&
    !isnan(hum)
  ) {

    chamberTemperature = temp;
    chamberHumidity = hum;

    lastValidSHTReading = millis();
  }


  // -----------------------------------------------------------
  // LOAD CELL
  // -----------------------------------------------------------

  if (scale.is_ready()) {

    // Average several readings
    float weight =
      scale.get_units(5);

    // Remove tiny zero drift
    if (
      weight > -0.5 &&
      weight < 0.5
    ) {

      weight = 0;
    }

    currentWeight = weight;
  }


  // -----------------------------------------------------------
  // ESTIMATED MOISTURE
  // -----------------------------------------------------------

  if (
    systemState == DRYING ||
    systemState == COOLING ||
    systemState == COMPLETE
  ) {

    estimatedMoisture =
      calculateEstimatedMoisture();
  }
}


// =============================================================
// SHT SENSOR HEALTH
// =============================================================

bool shtSensorHealthy() {

  if (!shtOK)
    return false;

  if (
    millis() - lastValidSHTReading
    > 10000UL
  ) {

    return false;
  }

  return true;
}


// =============================================================
// DRYING CONTROL
// =============================================================

void controlDrying() {

  if (systemState != DRYING)
    return;


  // Fan remains ON during normal drying
  setFan(true);


  // -----------------------------------------------------------
  // TEMPERATURE / SENSOR SAFETY
  // -----------------------------------------------------------

  if (!shtSensorHealthy()) {

    // If chamber temperature sensor fails:
    // heater OFF, fan ON.

    setHeater(false);
  }

  else {

    if (
      chamberTemperature >=
      MAX_CHAMBER_TEMP_C
    ) {

      setHeater(false);
    }

    else if (
      chamberTemperature <=
      HEATER_RESUME_TEMP_C
    ) {

      setHeater(true);
    }
  }


  // -----------------------------------------------------------
  // TARGET MOISTURE CHECK
  // -----------------------------------------------------------

  if (
    !isnan(estimatedMoisture) &&
    estimatedMoisture <= targetMoisture
  ) {

    targetCounter++;
  }

  else {

    targetCounter = 0;
  }


  // -----------------------------------------------------------
  // TARGET CONFIRMED
  // -----------------------------------------------------------

  if (
    targetCounter >=
    TARGET_CONFIRM_COUNT
  ) {

    dryingDurationMs =
      millis() - dryingStartMs;

    // Heater stops immediately
    setHeater(false);

    // Fan continues for 2 minutes
    setFan(true);

    coolingStartMs = millis();

    systemState = COOLING;

    Serial.println();
    Serial.println(
      "*** TARGET MOISTURE REACHED ***"
    );

    Serial.print(
      "Final estimated MC: "
    );

    Serial.print(
      estimatedMoisture,
      2
    );

    Serial.println("%");

    Serial.println(
      "Heater OFF"
    );

    Serial.println(
      "Fan cooling period started."
    );
  }
}


// =============================================================
// COOLING CONTROL
// =============================================================

void controlCooling() {

  if (systemState != COOLING)
    return;

  setHeater(false);
  setFan(true);

  if (
    millis() - coolingStartMs
    >= COOLING_TIME_MS
  ) {

    setFan(false);

    systemState = COMPLETE;

    sendTelemetryToDashboard();

    Serial.println();
    Serial.println(
      "*** DRYING COMPLETE ***"
    );
  }
}


// =============================================================
// CSV RESEARCH DATA LOG
// =============================================================

void printCSVHeader() {

  Serial.println();
  Serial.println(
    "CSV_HEADER,"
    "batchId,"
    "elapsedSec,"
    "temperatureC,"
    "humidityPct,"
    "weightG,"
    "estimatedMC,"
    "targetMC,"
    "heater,"
    "fan,"
    "predictedRDTMin,"
    "rdtApiOnline,"
    "state"
  );
}


const char* stateName() {

  if (systemState == DRYING)
    return "DRYING";

  if (systemState == COOLING)
    return "COOLING";

  if (systemState == COMPLETE)
    return "COMPLETE";

  if (systemState == ABORTED)
    return "ABORTED";

  return "SETUP";
}


void logResearchData() {

  if (
    systemState != DRYING &&
    systemState != COOLING
  ) {

    return;
  }

  unsigned long elapsedSeconds =
    getDryingElapsed() / 1000UL;

  Serial.print("CSV,");

  Serial.print(batchId);
  Serial.print(",");

  Serial.print(elapsedSeconds);
  Serial.print(",");

  Serial.print(chamberTemperature, 2);
  Serial.print(",");

  Serial.print(chamberHumidity, 2);
  Serial.print(",");

  Serial.print(currentWeight, 2);
  Serial.print(",");

  Serial.print(estimatedMoisture, 2);
  Serial.print(",");

  Serial.print(targetMoisture, 2);
  Serial.print(",");

  Serial.print(
    heaterOn ? 1 : 0
  );

  Serial.print(",");

  Serial.print(
    fanOn ? 1 : 0
  );

  Serial.print(",");

  if (rdtPredictionValid) {
    Serial.print(getDisplayedRDTMinutes(), 1);
  }
  else {
    Serial.print(-1);
  }

  Serial.print(",");

  Serial.print(
    rdtApiOnline ? 1 : 0
  );

  Serial.print(",");

  Serial.println(
    stateName()
  );
}


// =============================================================
// STARTUP SCREEN
// =============================================================

void showStartupScreen() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("LOAD CELL SETUP");

  display.setCursor(0, 18);
  display.println("Empty the tray");

  display.setCursor(0, 34);

  if (calibrationValid) {

    display.println("* = Tare / Continue");
  }

  else {

    display.println("Calibration needed");
  }

  display.setCursor(0, 50);
  display.println("D = Calibrate known weight");

  display.display();
}


// =============================================================
// ENTER KNOWN CALIBRATION WEIGHT
// =============================================================

void showCalibrationWeightInput() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("KNOWN CAL WEIGHT");

  display.setCursor(0, 18);
  display.println("Enter grams:");

  display.setCursor(0, 34);
  display.setTextSize(2);

  if (inputBuffer.length() == 0) {
    display.print("--");
  } else {
    display.print(inputBuffer);
  }

  display.print("g");
  display.setTextSize(1);

  display.setCursor(0, 54);
  display.println("* OK   # Back");

  display.display();
}


// =============================================================
// CALIBRATION SCREENS
// =============================================================

void showCalibrationEmpty() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("CALIBRATION");

  display.setCursor(0, 18);
  display.println("Remove all weight");

  display.setCursor(0, 34);
  display.println("Tray must be EMPTY");

  display.setCursor(0, 50);
  display.println("* Tare   # Back");

  display.display();
}


void showCalibrationWeight() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("PLACE CAL WEIGHT");

  display.setCursor(0, 18);
  display.print("Known: ");
  display.print(calibrationReferenceWeight, 1);
  display.println("g");

  display.setCursor(0, 34);
  display.println("Place in center");

  display.setCursor(0, 50);
  display.println("* Cal   # Back");

  display.display();
}


// =============================================================
// INPUT SCREENS
// =============================================================

void showInitialMoistureScreen() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("Place cinnamon");

  display.setCursor(0, 18);
  display.println("Initial Moisture:");

  display.setCursor(0, 38);

  display.setTextSize(2);

  if (inputBuffer.length() == 0) {

    display.print("--");
  }

  else {

    display.print(inputBuffer);
  }

  display.print("%");

  display.setTextSize(1);

  display.setCursor(0, 54);
  display.println("* OK   # Back");

  display.display();
}


void showTargetMoistureScreen() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("Target Moisture:");

  display.setCursor(0, 20);

  display.setTextSize(2);

  if (inputBuffer.length() == 0) {

    display.print("--");
  }

  else {

    display.print(inputBuffer);
  }

  display.print("%");

  display.setTextSize(1);

  display.setCursor(0, 52);
  display.println("* Confirm  # Back");

  display.display();
}


// =============================================================
// PLACE CINNAMON / CAPTURE INITIAL WEIGHT
// =============================================================

void showPlaceCinnamonScreen() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("PLACE CINNAMON");

  display.setCursor(0, 18);
  display.println("Put bundle on tray");

  display.setCursor(0, 34);
  display.print("Live Wt: ");
  display.print(currentWeight, 1);
  display.println("g");

  display.setCursor(0, 50);
  display.println("* Capture  # Back");

  display.display();
}


// =============================================================
// READY SCREEN
// =============================================================

void showReadyScreen() {

  clearOLED();

  display.setCursor(0, 0);

  display.print("Init:");
  display.print(initialMoisture, 0);
  display.print("%  Tgt:");
  display.print(targetMoisture, 0);
  display.println("%");

  display.setCursor(0, 20);

  display.print("Weight: ");
  display.print(currentWeight, 1);
  display.println("g");

  display.setCursor(0, 39);
  display.println("* = START DRYING");

  display.setCursor(0, 54);
  display.println("# = Back");

  display.display();
}


// =============================================================
// PAGE A - HOME
// =============================================================

void showHomeScreen() {

  clearOLED();

  display.setCursor(0, 0);

  display.print("MC:");

  if (isnan(estimatedMoisture))
    display.print("--");
  else
    display.print(estimatedMoisture, 1);

  display.print("% Tgt:");
  display.print(targetMoisture, 0);
  display.print("%");


  display.setCursor(0, 16);

  display.print("T:");

  if (isnan(chamberTemperature))
    display.print("--");
  else
    display.print(chamberTemperature, 1);

  display.print("C RH:");

  if (isnan(chamberHumidity))
    display.print("--");
  else
    display.print(chamberHumidity, 0);

  display.print("%");


  display.setCursor(0, 32);

  display.print("Wt:");
  display.print(currentWeight, 1);
  display.print("g");


  display.setCursor(0, 48);

  display.print("Time:");
  display.print(
    formatTime(
      getDryingElapsed()
    )
  );

  display.display();
}


// =============================================================
// PAGE B - SENSOR DETAILS
// =============================================================

void showSensorScreen() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("SENSOR DETAILS");

  display.setCursor(0, 16);

  display.print("Temp: ");
  display.print(chamberTemperature, 1);
  display.println(" C");

  display.setCursor(0, 30);

  display.print("RH:   ");
  display.print(chamberHumidity, 1);
  display.println(" %");

  display.setCursor(0, 44);

  display.print("Weight: ");
  display.print(currentWeight, 1);
  display.println("g");

  display.setCursor(0, 56);
  display.println("Ambient SHT: later");

  display.display();
}


// =============================================================
// PAGE C - CONTROL STATUS
// =============================================================

void showControlScreen() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("CONTROL STATUS");

  display.setCursor(0, 17);

  display.print("Heater: ");
  display.println(
    heaterOn ? "ON" : "OFF"
  );

  display.setCursor(0, 31);

  display.print("Fan:    ");
  display.println(
    fanOn ? "ON" : "OFF"
  );

  display.setCursor(0, 45);

  display.print("Mode: ");
  display.println(
    stateName()
  );

  display.setCursor(0, 57);
  display.println("0 = EMERGENCY STOP");

  display.display();
}


// =============================================================
// PAGE D - TIME / RDT
// =============================================================

void showTimeRDTScreen() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("TIME / RDT");

  display.setCursor(0, 18);

  display.print("Elapsed: ");
  display.println(
    formatTime(
      getDryingElapsed()
    )
  );

  display.setCursor(0, 34);
  display.print("RDT: ");

  if (rdtPredictionValid) {

    float shownRDT =
      getDisplayedRDTMinutes();

    int totalMinutes =
      (int)round(shownRDT);

    int hours =
      totalMinutes / 60;

    int minutes =
      totalMinutes % 60;

    display.print(hours);
    display.print("h ");

    if (minutes < 10)
      display.print("0");

    display.print(minutes);
    display.print("m");
  }

  else {

    if (WiFi.status() == WL_CONNECTED)
      display.print("Pending");
    else
      display.print("Unavailable");
  }

  display.setCursor(0, 50);

  display.print("ML: ");

  if (rdtApiOnline)
    display.print("ONLINE");
  else if (WiFi.status() == WL_CONNECTED)
    display.print("WAIT/OFFLINE");
  else
    display.print("NO WIFI");

  display.display();
}


// =============================================================
// COOLING SCREEN
// =============================================================

void showCoolingScreen() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("TARGET REACHED");

  display.setCursor(0, 16);
  display.println("Heater: OFF");

  display.setCursor(0, 29);
  display.println("Fan: COOLING");

  unsigned long elapsed =
    millis() - coolingStartMs;

  unsigned long remaining = 0;

  if (elapsed < COOLING_TIME_MS) {

    remaining =
      COOLING_TIME_MS - elapsed;
  }

  display.setCursor(0, 44);

  display.print("MC: ");
  display.print(
    estimatedMoisture,
    1
  );

  display.print("%");

  display.setCursor(0, 56);

  display.print("Remain: ");
  display.print(
    remaining / 1000UL
  );

  display.print("s");

  display.display();
}


// =============================================================
// COMPLETE SCREEN
// =============================================================

void showCompleteScreen() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("DRYING COMPLETE");

  display.setCursor(0, 18);

  display.print("Final MC:");
  display.print(
    estimatedMoisture,
    1
  );

  display.println("%");

  display.setCursor(0, 34);

  display.print("Time:");
  display.println(
    formatTime(
      dryingDurationMs
    )
  );

  display.setCursor(0, 50);
  display.println("* = New Batch");

  display.display();
}


// =============================================================
// ABORT SCREEN
// =============================================================

void showAbortedScreen() {

  clearOLED();

  display.setCursor(0, 8);
  display.println("SESSION STOPPED");

  display.setCursor(0, 28);
  display.println("Heater OFF");
  display.println("Fan OFF");

  display.setCursor(0, 50);
  display.println("* = New Batch");

  display.display();
}


// =============================================================
// DISPLAY MANAGER
// =============================================================

void updateDisplayScreen() {

  if (!oledOK)
    return;

  switch (systemState) {

    case STARTUP_TARE:
      showStartupScreen();
      break;

    case ENTER_CAL_WEIGHT:
      showCalibrationWeightInput();
      break;

    case CALIBRATE_EMPTY:
      showCalibrationEmpty();
      break;

    case CALIBRATE_WEIGHT:
      showCalibrationWeight();
      break;

    case ENTER_INITIAL_MC:
      showInitialMoistureScreen();
      break;

    case ENTER_TARGET_MC:
      showTargetMoistureScreen();
      break;

    case PLACE_CINNAMON:
      showPlaceCinnamonScreen();
      break;

    case READY_TO_START:
      showReadyScreen();
      break;

    case DRYING:

      if (activePage == 'A')
        showHomeScreen();

      else if (activePage == 'B')
        showSensorScreen();

      else if (activePage == 'C')
        showControlScreen();

      else if (activePage == 'D')
        showTimeRDTScreen();

      break;

    case COOLING:
      showCoolingScreen();
      break;

    case COMPLETE:
      showCompleteScreen();
      break;

    case ABORTED:
      showAbortedScreen();
      break;
  }
}


// =============================================================
// START NEW BATCH
// =============================================================

void resetForNewBatch() {

  stopEverything();

  initialMoisture = 0;
  targetMoisture = 0;

  initialWeight = 0;
  currentWeight = 0;

  dryMass = 0;
  targetWeight = 0;

  estimatedMoisture = NAN;

  dryingStartMs = 0;
  dryingDurationMs = 0;
  coolingStartMs = 0;

  predictedRDTMinutes = -1.0;
  rdtPredictionValid = false;
  rdtApiOnline = false;
  firstRDTPredictionAttempted = false;
  lastRDTAttemptMs = 0;
  lastRDTSuccessMs = 0;

  targetCounter = 0;

  calibrationReferenceWeight = 0.0;
  inputBuffer = "";

  activePage = 'A';

  systemState =
    STARTUP_TARE;
}


// =============================================================
// START DRYING SESSION
// =============================================================

void startDryingSession() {

  if (!scale.is_ready()) {

    Serial.println(
      "Cannot start: HX711 unavailable."
    );

    return;
  }

  if (!shtSensorHealthy()) {

    Serial.println(
      "Cannot start: SHT30 unavailable."
    );

    return;
  }


  // Initial weight must already have been captured on the
  // PLACE_CINNAMON screen before the user can reach START.

  if (initialWeight < 5.0) {

    Serial.println(
      "ERROR: Initial weight was not captured."
    );

    return;
  }

  // Refresh live weight, but DO NOT overwrite initialWeight.
  currentWeight = scale.get_units(20);

  estimatedMoisture =
    initialMoisture;


  // -----------------------------------------------------------
  // Generate batch ID
  // -----------------------------------------------------------

  batchCounter++;

  preferences.putUInt(
    "batchCount",
    batchCounter
  );

  char batchBuffer[10];

  snprintf(
    batchBuffer,
    sizeof(batchBuffer),
    "B%03u",
    batchCounter
  );

  batchId =
    String(batchBuffer);


  // -----------------------------------------------------------
  // Start timer
  // -----------------------------------------------------------

  dryingStartMs =
    millis();

  // New drying session = new RDT prediction sequence.
  predictedRDTMinutes = -1.0;
  rdtPredictionValid = false;
  rdtApiOnline = false;
  firstRDTPredictionAttempted = false;
  lastRDTAttemptMs = dryingStartMs;
  lastRDTSuccessMs = 0;

  targetCounter = 0;

  activePage = 'A';

  systemState =
    DRYING;


  // -----------------------------------------------------------
  // Start drying
  // -----------------------------------------------------------

  setFan(true);

  // Heater starts unless temperature already unsafe

  if (
    chamberTemperature <
    MAX_CHAMBER_TEMP_C
  ) {

    setHeater(true);
  }

  else {

    setHeater(false);
  }


  // -----------------------------------------------------------
  // Serial research information
  // -----------------------------------------------------------

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "DRYING SESSION STARTED"
  );

  Serial.print(
    "Batch ID: "
  );

  Serial.println(batchId);

  Serial.print(
    "Initial moisture: "
  );

  Serial.print(
    initialMoisture
  );

  Serial.println("%");

  Serial.print(
    "Target moisture: "
  );

  Serial.print(
    targetMoisture
  );

  Serial.println("%");

  Serial.print(
    "Initial weight: "
  );

  Serial.print(
    initialWeight,
    2
  );

  Serial.println(" g");

  Serial.print(
    "Calculated dry mass: "
  );

  Serial.print(
    dryMass,
    2
  );

  Serial.println(" g");

  Serial.print(
    "Calculated target weight: "
  );

  Serial.print(
    targetWeight,
    2
  );

  Serial.println(" g");

  Serial.println(
    "================================"
  );

  printCSVHeader();

  lastLogTime =
    millis() - LOG_INTERVAL_MS;

    // Allow first dashboard telemetry upload immediately
  lastTelemetryUploadMs =
    millis() - TELEMETRY_UPLOAD_INTERVAL_MS;
}


// =============================================================
// KEYPAD HANDLER
// =============================================================

void handleKey(char key) {

  Serial.print(
    "KEY: "
  );

  Serial.println(key);


  // ===========================================================
  // STARTUP / TARE
  // ===========================================================

  if (
    systemState ==
    STARTUP_TARE
  ) {

    if (key == 'D') {

      inputBuffer = "";

      systemState =
        ENTER_CAL_WEIGHT;

      return;
    }


    if (key == '*') {

      if (!calibrationValid) {

        Serial.println(
          "Calibration required. Press D."
        );

        return;
      }

      if (!scale.is_ready()) {

        Serial.println(
          "HX711 not ready."
        );

        return;
      }

      Serial.println(
        "Taring empty tray..."
      );

      scale.set_scale(
        calibrationFactor
      );

      scale.tare(20);

      Serial.println(
        "Tare complete."
      );

      inputBuffer = "";

      systemState =
        ENTER_INITIAL_MC;

      return;
    }
  }


  // ===========================================================
  // ENTER KNOWN CALIBRATION WEIGHT
  // ===========================================================

  if (
    systemState ==
    ENTER_CAL_WEIGHT
  ) {

    if (
      key >= '0' &&
      key <= '9'
    ) {

      if (
        inputBuffer.length()
        < 4
      ) {
        inputBuffer += key;
      }

      return;
    }

    if (key == '#') {

      inputBuffer = "";
      systemState = STARTUP_TARE;
      return;
    }

    if (key == '*') {

      if (inputBuffer.length() == 0) {
        return;
      }

      calibrationReferenceWeight =
        inputBuffer.toFloat();

      if (
        calibrationReferenceWeight < 5.0 ||
        calibrationReferenceWeight > 5000.0
      ) {

        Serial.println(
          "Invalid calibration weight."
        );

        inputBuffer = "";
        return;
      }

      Serial.print(
        "Known calibration weight: "
      );
      Serial.print(
        calibrationReferenceWeight,
        1
      );
      Serial.println(" g");

      inputBuffer = "";
      systemState = CALIBRATE_EMPTY;
      return;
    }
  }


  // ===========================================================
  // CALIBRATION - EMPTY TRAY
  // ===========================================================

  if (
    systemState ==
    CALIBRATE_EMPTY
  ) {

    if (key == '#') {

      inputBuffer = "";

      systemState =
        ENTER_CAL_WEIGHT;

      return;
    }


    if (key == '*') {

      scale.set_scale(1.0);

      scale.tare(20);

      Serial.println(
        "Empty tray tared for calibration."
      );

      systemState =
        CALIBRATE_WEIGHT;

      return;
    }
  }


  // ===========================================================
  // CALIBRATION - USER-ENTERED KNOWN WEIGHT
  // ===========================================================

  if (
    systemState ==
    CALIBRATE_WEIGHT
  ) {

    if (key == '#') {

      systemState =
        CALIBRATE_EMPTY;

      return;
    }


    if (key == '*') {

      if (calibrationReferenceWeight <= 0.0) {

        Serial.println(
          "ERROR: Known calibration weight is missing."
        );

        systemState =
          ENTER_CAL_WEIGHT;

        return;
      }

      Serial.print(
        "Calibrating with known weight: "
      );
      Serial.print(
        calibrationReferenceWeight,
        1
      );
      Serial.println(" g");

      delay(1500);

      scale.set_scale(1.0);

      float raw =
        scale.get_units(30);

      calibrationFactor =
        raw /
        calibrationReferenceWeight;

      scale.set_scale(
        calibrationFactor
      );

      preferences.putFloat(
        "calFactor",
        calibrationFactor
      );

      calibrationValid =
        true;


      float verification =
        scale.get_units(20);


      Serial.print(
        "Calibration factor: "
      );

      Serial.println(
        calibrationFactor,
        6
      );

      Serial.print(
        "Verification weight: "
      );

      Serial.print(
        verification,
        2
      );

      Serial.println(" g");

      Serial.println(
        "Calibration saved. Remove the reference weight."
      );

      delay(1500);

      systemState =
        STARTUP_TARE;

      return;
    }
  }


  // ===========================================================
  // INITIAL MOISTURE INPUT
  // ===========================================================

  if (
    systemState ==
    ENTER_INITIAL_MC
  ) {

    if (
      key >= '0' &&
      key <= '9'
    ) {

      if (
        inputBuffer.length()
        < 3
      ) {

        inputBuffer += key;
      }

      return;
    }


    if (key == '#') {

      inputBuffer = "";

      systemState =
        STARTUP_TARE;

      return;
    }


    if (key == '*') {

      if (
        inputBuffer.length()
        == 0
      ) {

        return;
      }

      float value =
        inputBuffer.toFloat();


      if (
        value <= 0 ||
        value >= 100
      ) {

        Serial.println(
          "Invalid initial moisture."
        );

        inputBuffer = "";

        return;
      }


      initialMoisture =
        value;

      inputBuffer = "";

      systemState =
        ENTER_TARGET_MC;

      return;
    }
  }


  // ===========================================================
  // TARGET MOISTURE INPUT
  // ===========================================================

  if (
    systemState ==
    ENTER_TARGET_MC
  ) {

    if (
      key >= '0' &&
      key <= '9'
    ) {

      if (
        inputBuffer.length()
        < 3
      ) {

        inputBuffer += key;
      }

      return;
    }


    if (key == '#') {

      inputBuffer = "";

      systemState =
        ENTER_INITIAL_MC;

      return;
    }


    if (key == '*') {

      if (
        inputBuffer.length()
        == 0
      ) {

        return;
      }

      float value =
        inputBuffer.toFloat();


      if (
        value <= 0 ||
        value >= initialMoisture
      ) {

        Serial.println(
          "Target must be below initial MC."
        );

        inputBuffer = "";

        return;
      }


      targetMoisture =
        value;

      inputBuffer = "";

      // Ask the user to place the cinnamon and capture
      // the actual starting weight before START is allowed.
      systemState =
        PLACE_CINNAMON;

      return;
    }
  }


  // ===========================================================
  // PLACE CINNAMON / CAPTURE INITIAL WEIGHT
  // ===========================================================

  if (
    systemState ==
    PLACE_CINNAMON
  ) {

    if (key == '#') {

      initialWeight = 0.0;
      currentWeight = 0.0;
      inputBuffer = "";

      systemState =
        ENTER_TARGET_MC;

      return;
    }

    if (key == '*') {

      if (!scale.is_ready()) {

        Serial.println(
          "HX711 not ready."
        );

        return;
      }

      float capturedWeight =
        scale.get_units(20);

      if (capturedWeight < 5.0) {

        Serial.println(
          "ERROR: Cinnamon weight is too low/zero. "
          "Check tare and place the bundle before pressing *."
        );

        return;
      }

      initialWeight =
        capturedWeight;

      currentWeight =
        capturedWeight;

      // Calculate dry matter and target weight NOW so the
      // confirmation screen has valid values.
      dryMass =
        initialWeight *
        (1.0 -
         initialMoisture / 100.0);

      targetWeight =
        dryMass /
        (1.0 -
         targetMoisture / 100.0);

      estimatedMoisture =
        initialMoisture;

      Serial.print(
        "Captured initial cinnamon weight: "
      );
      Serial.print(
        initialWeight,
        2
      );
      Serial.println(" g");

      Serial.print(
        "Calculated target weight: "
      );
      Serial.print(
        targetWeight,
        2
      );
      Serial.println(" g");

      systemState =
        READY_TO_START;

      return;
    }
  }


  // ===========================================================
  // READY TO START
  // ===========================================================

  if (
    systemState ==
    READY_TO_START
  ) {

    if (key == '#') {

      initialWeight = 0.0;

      systemState =
        PLACE_CINNAMON;

      return;
    }


    if (key == '*') {

      startDryingSession();

      return;
    }
  }


  // ===========================================================
  // ACTIVE DRYING
  // ===========================================================

  if (
    systemState == DRYING
  ) {

    if (
      key == 'A' ||
      key == 'B' ||
      key == 'C' ||
      key == 'D'
    ) {

      activePage = key;

      return;
    }


    // Emergency stop
    if (key == '0') {

      dryingDurationMs =
        millis() -
        dryingStartMs;

      stopEverything();

      systemState =
        ABORTED;

      sendTelemetryToDashboard();

      Serial.println(
        "*** EMERGENCY STOP ***"
      );

      return;
    }
  }


  // ===========================================================
  // COOLING
  // ===========================================================

  if (
    systemState ==
    COOLING
  ) {

    if (key == '0') {

      stopEverything();

      systemState =
        ABORTED;

      return;
    }
  }


  // ===========================================================
  // COMPLETE / ABORTED
  // ===========================================================

  if (
    systemState == COMPLETE ||
    systemState == ABORTED
  ) {

    if (key == '*') {

      resetForNewBatch();

      return;
    }
  }
}


// =============================================================
// SETUP
// =============================================================

void setup() {

  Serial.begin(115200);

  delay(1200);

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "SMART CINNAMON DEHYDRATION"
  );

  Serial.println(
    "SYSTEM STARTING"
  );

  Serial.println(
    "================================"
  );


  // ===========================================================
  // RELAYS - SAFE OFF DURING BOOT
  // ===========================================================

  pinMode(
    HEATER_RELAY_PIN,
    OUTPUT
  );

  pinMode(
    FAN_RELAY_PIN,
    OUTPUT
  );

  stopEverything();


  // ===========================================================
  // WI-FI FOR RDT MODEL
  // ===========================================================

  // Failure to connect does NOT stop the chamber.
  // Heater/fan safety and normal drying remain local on ESP32.
  connectWiFiAtStartup();


  // ===========================================================
  // OLED
  // ===========================================================

  OLEDBus.begin(
    OLED_SDA,
    OLED_SCL,
    100000
  );

  oledOK =
    display.begin(
      0x3C,
      true
    );


  Serial.print(
    "OLED: "
  );

  Serial.println(
    oledOK ?
    "OK" :
    "FAIL"
  );


  // ===========================================================
  // SHT30
  // ===========================================================

  SHTBus.begin(
    SHT_SDA,
    SHT_SCL,
    100000
  );

  shtOK =
    sht31.begin(0x44);


  Serial.print(
    "SHT30: "
  );

  Serial.println(
    shtOK ?
    "OK" :
    "FAIL"
  );


  if (shtOK) {

    lastValidSHTReading =
      millis();
  }


  // ===========================================================
  // HX711
  // ===========================================================

  scale.begin(
    HX711_DT,
    HX711_SCK
  );


  delay(500);


  hxOK =
    scale.is_ready();


  Serial.print(
    "HX711: "
  );

  Serial.println(
    hxOK ?
    "OK" :
    "FAIL"
  );


  // ===========================================================
  // PREFERENCES
  // ===========================================================

  preferences.begin(
    "loadcell",
    false
  );


  calibrationFactor =
    preferences.getFloat(
      "calFactor",
      0.0
    );


  if (
    fabs(calibrationFactor)
    > 0.0001
  ) {

    calibrationValid =
      true;

    scale.set_scale(
      calibrationFactor
    );


    Serial.print(
      "Saved calibration factor: "
    );

    Serial.println(
      calibrationFactor,
      6
    );
  }

  else {

    calibrationValid =
      false;

    Serial.println(
      "No saved calibration."
    );
  }


  batchCounter =
    preferences.getUInt(
      "batchCount",
      0
    );


  // ===========================================================
  // INITIAL SENSOR READ
  // ===========================================================

  updateSensors();


  // ===========================================================
  // STARTUP OLED
  // ===========================================================

  if (oledOK) {

    clearOLED();

    display.setCursor(0, 8);
    display.println(
      "SMART CINNAMON"
    );

    display.setCursor(0, 25);
    display.println(
      "DEHYDRATION"
    );

    display.setCursor(0, 45);
    display.println(
      "Starting..."
    );

    display.display();

    delay(1500);
  }


  systemState =
    STARTUP_TARE;

  updateDisplayScreen();
}


// =============================================================
// LOOP
// =============================================================

void loop() {

  // -----------------------------------------------------------
  // KEYPAD
  // -----------------------------------------------------------

  char key =
    keypad.getKey();

  if (key) {

    handleKey(key);

    updateDisplayScreen();
  }


  // -----------------------------------------------------------
  // SENSOR UPDATE
  // -----------------------------------------------------------

  if (
    millis() -
    lastSensorUpdate
    >= SENSOR_INTERVAL_MS
  ) {

    lastSensorUpdate =
      millis();

    updateSensors();

    controlDrying();

    controlCooling();
  }


  // -----------------------------------------------------------
  // WI-FI / RDT MODEL
  // -----------------------------------------------------------

  maintainWiFi();

  updateRDTPredictionSchedule();

  updateDashboardTelemetrySchedule();


  // -----------------------------------------------------------
  // OLED UPDATE
  // -----------------------------------------------------------

  if (
    millis() -
    lastDisplayUpdate
    >= DISPLAY_INTERVAL_MS
  ) {

    lastDisplayUpdate =
      millis();

    updateDisplayScreen();
  }


  // -----------------------------------------------------------
  // RESEARCH DATA LOGGING
  // -----------------------------------------------------------

  if (
    (
      systemState == DRYING ||
      systemState == COOLING
    ) &&
    (
      millis() -
      lastLogTime
      >= LOG_INTERVAL_MS
    )
  ) {

    lastLogTime =
      millis();

    logResearchData();
  }
}