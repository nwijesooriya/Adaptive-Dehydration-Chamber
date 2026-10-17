#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_SHT31.h>
#include <HX711.h>
#include <Keypad.h>
#include <Preferences.h>

// =============================================================
// SMART CINNAMON DEHYDRATION CHAMBER
// ESP32-S3 DevKitC-1 N16R8
//
// FINAL PP1 DRYING FIRMWARE - V1
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
//   Heater -> GPIO 10
//   Fan    -> GPIO 11
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
#define HEATER_RELAY_PIN 10
#define FAN_RELAY_PIN    11

#define RELAY_ON  LOW
#define RELAY_OFF HIGH


// =============================================================
// SETTINGS
// =============================================================

// Calibration reference
const float CALIBRATION_WEIGHT_G = 100.0;

// Cooling / purge time after target MC reached
const unsigned long COOLING_TIME_MS = 120000UL; // 2 minutes

// Sensor update
const unsigned long SENSOR_INTERVAL_MS = 1000UL;

// OLED refresh
const unsigned long DISPLAY_INTERVAL_MS = 500UL;

// Research-data logging interval
const unsigned long LOG_INTERVAL_MS = 60000UL; // 1 minute

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

  CALIBRATE_EMPTY,
  CALIBRATE_WEIGHT,

  ENTER_INITIAL_MC,
  ENTER_TARGET_MC,

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
  display.println("D = Calibrate 100g");

  display.display();
}


// =============================================================
// CALIBRATION SCREENS
// =============================================================

void showCalibrationEmpty() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("CALIBRATION");

  display.setCursor(0, 20);
  display.println("EMPTY the tray");

  display.setCursor(0, 38);
  display.println("Press * to tare");

  display.setCursor(0, 54);
  display.println("# = Cancel");

  display.display();
}


void showCalibrationWeight() {

  clearOLED();

  display.setCursor(0, 0);
  display.println("CALIBRATION");

  display.setCursor(0, 18);
  display.println("Place 100g soap");

  display.setCursor(0, 36);
  display.println("in tray center");

  display.setCursor(0, 52);
  display.println("Press *");

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

  display.println("RDT: Pending");

  display.setCursor(0, 50);

  display.print("TargetWt:");
  display.print(targetWeight, 1);
  display.print("g");

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

  targetCounter = 0;

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


  // Capture accurate initial weight

  currentWeight =
    scale.get_units(20);

  initialWeight =
    currentWeight;


  if (initialWeight < 5.0) {

    Serial.println(
      "ERROR: Initial weight too low."
    );

    return;
  }


  // -----------------------------------------------------------
  // Calculate dry matter
  // -----------------------------------------------------------

  dryMass =
    initialWeight *
    (1.0 -
     initialMoisture / 100.0);


  // -----------------------------------------------------------
  // Calculate predicted target weight
  // -----------------------------------------------------------

  targetWeight =
    dryMass /
    (1.0 -
     targetMoisture / 100.0);


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

      systemState =
        CALIBRATE_EMPTY;

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
  // CALIBRATION - EMPTY TRAY
  // ===========================================================

  if (
    systemState ==
    CALIBRATE_EMPTY
  ) {

    if (key == '#') {

      systemState =
        STARTUP_TARE;

      return;
    }


    if (key == '*') {

      scale.set_scale(1.0);

      scale.tare(20);

      systemState =
        CALIBRATE_WEIGHT;

      return;
    }
  }


  // ===========================================================
  // CALIBRATION - 100g
  // ===========================================================

  if (
    systemState ==
    CALIBRATE_WEIGHT
  ) {

    if (key == '#') {

      systemState =
        STARTUP_TARE;

      return;
    }


    if (key == '*') {

      Serial.println(
        "Calibrating with 100g..."
      );

      delay(1500);

      scale.set_scale(1.0);

      float raw =
        scale.get_units(30);

      calibrationFactor =
        raw /
        CALIBRATION_WEIGHT_G;

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

      inputBuffer = "";

      systemState =
        ENTER_TARGET_MC;

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