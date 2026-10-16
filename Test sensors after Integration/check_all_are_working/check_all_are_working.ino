#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Keypad.h>

// ======================================================
// RELAY INTEGRATION TEST
// ESP32-S3 DevKitC-1 N16R8
// ======================================================

// ---------------- OLED ----------------
#define OLED_SDA 8
#define OLED_SCL 9

TwoWire OLEDBus = TwoWire(0);

Adafruit_SH1106G display(
  128,
  64,
  &OLEDBus,
  -1
);

// ---------------- RELAYS ----------------
#define RELAY1_PIN 10
#define RELAY2_PIN 11

// Your relay board is ACTIVE LOW
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

bool relay1State = false;
bool relay2State = false;

// ---------------- KEYPAD ----------------

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

bool oledOK = false;

// ======================================================
// UPDATE RELAYS
// ======================================================

void updateRelays() {

  digitalWrite(
    RELAY1_PIN,
    relay1State ? RELAY_ON : RELAY_OFF
  );

  digitalWrite(
    RELAY2_PIN,
    relay2State ? RELAY_ON : RELAY_OFF
  );
}

// ======================================================
// OLED STATUS
// ======================================================

void showStatus() {

  Serial.println();
  Serial.println("====================");

  Serial.print("Relay 1 / Heater: ");
  Serial.println(
    relay1State ? "ON" : "OFF"
  );

  Serial.print("Relay 2 / Fan: ");
  Serial.println(
    relay2State ? "ON" : "OFF"
  );

  Serial.println("====================");

  if (!oledOK)
    return;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("RELAY TEST");

  display.setCursor(0, 18);
  display.print("R1 Heater: ");
  display.println(
    relay1State ? "ON" : "OFF"
  );

  display.setCursor(0, 32);
  display.print("R2 Fan   : ");
  display.println(
    relay2State ? "ON" : "OFF"
  );

  display.setCursor(0, 48);
  display.println("1:R1 2:R2 3:ON");

  display.setCursor(0, 57);
  display.println("0:ALL OFF");

  display.display();
}

// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println(
    "ADAPTIVE CHAMBER - RELAY TEST"
  );

  // ------------------------------
  // SAFE START
  // ------------------------------

  pinMode(
    RELAY1_PIN,
    OUTPUT
  );

  pinMode(
    RELAY2_PIN,
    OUTPUT
  );

  // Start BOTH OFF
  digitalWrite(
    RELAY1_PIN,
    RELAY_OFF
  );

  digitalWrite(
    RELAY2_PIN,
    RELAY_OFF
  );

  relay1State = false;
  relay2State = false;

  Serial.println(
    "Both relays forced OFF at boot."
  );

  // ------------------------------
  // OLED
  // ------------------------------

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

  if (oledOK) {

    Serial.println(
      "OLED: OK"
    );

  } else {

    Serial.println(
      "OLED: FAIL"
    );
  }

  showStatus();

  Serial.println();
  Serial.println("KEYPAD COMMANDS");
  Serial.println("1 = Toggle Relay 1 / Heater");
  Serial.println("2 = Toggle Relay 2 / Fan");
  Serial.println("3 = BOTH ON");
  Serial.println("0 = BOTH OFF");
  Serial.println("D = Emergency BOTH OFF");
}

// ======================================================
// LOOP
// ======================================================

void loop() {

  char key =
    keypad.getKey();

  if (!key)
    return;

  Serial.print("Key pressed: ");
  Serial.println(key);

  // ------------------------------
  // RELAY 1
  // ------------------------------

  if (key == '1') {

    relay1State =
      !relay1State;

    updateRelays();

    Serial.print(
      "Relay 1 / Heater -> "
    );

    Serial.println(
      relay1State ? "ON" : "OFF"
    );
  }

  // ------------------------------
  // RELAY 2
  // ------------------------------

  else if (key == '2') {

    relay2State =
      !relay2State;

    updateRelays();

    Serial.print(
      "Relay 2 / Fan -> "
    );

    Serial.println(
      relay2State ? "ON" : "OFF"
    );
  }

  // ------------------------------
  // BOTH ON
  // ------------------------------

  else if (key == '3') {

    relay1State = true;
    relay2State = true;

    updateRelays();

    Serial.println(
      "BOTH RELAYS ON"
    );
  }

  // ------------------------------
  // BOTH OFF
  // ------------------------------

  else if (
    key == '0' ||
    key == 'D'
  ) {

    relay1State = false;
    relay2State = false;

    updateRelays();

    Serial.println(
      "BOTH RELAYS OFF"
    );
  }

  // ------------------------------
  // STATUS
  // ------------------------------

  else if (
    key == 'A' ||
    key == 'B' ||
    key == 'C'
  ) {

    Serial.println(
      "Displaying relay status."
    );
  }

  showStatus();
}