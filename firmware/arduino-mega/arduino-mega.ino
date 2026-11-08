
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_Fingerprint.h>
#include "lcd_status.h"

// ========================================
// CAMP OF COFFEE EMPLOYEE TRACKING SYSTEM
// Arduino Mega 2560 + RC522 + AS608 + ESP32
// ========================================

// RFID RC522 pins
#define RFID_SS 53
#define RFID_RST 5

// AS608 uses Serial2
// AS608 TX -> Mega RX2 (Pin 17)
// AS608 RX -> Mega TX2 (Pin 16)

// ESP32 uses Serial1
// Mega TX1 (Pin 18) -> ESP32 RX2 (GPIO16)
// IMPORTANT: Use a 5V-to-3.3V level shifter
// Mega RX1 (Pin 19) <- ESP32 TX2 (GPIO17)
// Mega GND <-> ESP32 GND

MFRC522 rfid(RFID_SS, RFID_RST);
Adafruit_Fingerprint finger(&Serial2);

// Registered test employee
const char* REGISTERED_UID = "A3 F0 70 33";
const uint16_t REGISTERED_FINGER_ID = 1;
const uint16_t EMPLOYEE_ID = 1;

// Timing configuration
const unsigned long FINGER_TIMEOUT = 30000;
const unsigned long RFID_COOLDOWN = 2000;

unsigned long lastRFIDScan = 0;
bool fingerprintReady = false;
uint16_t expectedFingerId = REGISTERED_FINGER_ID;
#include "enrollment.h"

// ========================================
// SETUP
// ========================================

void setup() {
  // PC Serial Monitor
  Serial.begin(9600);

  // Communication with ESP32
  Serial1.begin(9600);
  beginStatusLcd();

  Serial.println();
  Serial.println("================================");
  Serial.println("CAMP OF COFFEE ATTENDANCE SYSTEM");
  Serial.println("================================");

  // Initialize RFID reader
  pinMode(RFID_SS, OUTPUT);
  digitalWrite(RFID_SS, HIGH);

  SPI.begin();
  rfid.PCD_Init();
  delay(150);

  Serial.println("RFID RC522 Initialized");

  // Display RFID firmware version
  rfid.PCD_DumpVersionToSerial();

  // Initialize fingerprint sensor
  finger.begin(57600);
  lcdStatus(F("Starting sensor"), F("Please wait..."));
  delay(1000);

  if (finger.verifyPassword()) {
    fingerprintReady = true;
    if (finger.getParameters() != FINGERPRINT_OK) Serial.println(F("WARNING: sensor parameters unavailable; enrollment will require a successful retry."));

    Serial.println("AS608 Connected Successfully!");

    if (finger.getTemplateCount() == FINGERPRINT_OK) {
      Serial.print("Stored Fingerprints: ");
      Serial.println(finger.templateCount);
    }
  } else {
    Serial.println("ERROR: AS608 Not Detected!");
    lcdStatus(F("Sensor error"), F("Check AS608"));
  }

  Serial.println();
  Serial.println("SYSTEM READY");
  Serial.println("Please tap your RFID card.");
  Serial.println("ESP32 UART Communication Initialized.");
  Serial.println(F("ENROLLMENT firmware v2 ready - Serial1 RX19/TX18 at 9600."));
  if (fingerprintReady) showLcdReady();
}

// ========================================
// MAIN LOOP
// ========================================

void loop() {
  readBridgeCommands();
  refreshLcdReady();
  if (!fingerprintReady) {
    delay(1000);
    return;
  }

  if (millis() - lastRFIDScan < RFID_COOLDOWN) {
    delay(20);
    return;
  }

  checkRFID();
}

// ========================================
// RFID VERIFICATION
// ========================================

void checkRFID() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {
    if (i > 0) {
      uid += " ";
    }

    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }

    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  lastRFIDScan = millis();

  Serial.println();
  Serial.println("==============================");
  Serial.print("RFID UID: ");
  Serial.println(uid);

  // Validate registered RFID card
  if (!(expectedFingerId = cardFinger(uid))) {
    Serial.println("ACCESS DENIED!");
    Serial.println("RFID card is not registered.");
    Serial.println("Tap a registered card.");
    lcdStatus(F("Card not found"), F("Tap valid card"), RFID_COOLDOWN);
    return;
  }

  Serial.println("RFID VERIFIED!");
  Serial.println("Place your registered finger on AS608.");
  Serial.println("You have 30 seconds.");
  lcdStatus(F("RFID verified"), F("Place finger"));

  bool verified = verifyFingerprint();

  if (verified) {
    Serial.println();
    Serial.println("==============================");
    Serial.println("ATTENDANCE VERIFIED!");
    Serial.print("Employee ID: ");
    Serial.println(EMPLOYEE_ID);

    Serial.print("RFID UID: ");
    Serial.println(uid);

    Serial.print("Fingerprint ID: ");
    Serial.println(expectedFingerId);

    Serial.println("==============================");

    // ====================================
    // SEND ATTENDANCE DATA TO ESP32
    // ====================================

    String attendanceData = "ATTENDANCE_VERIFIED";
    attendanceData += ",EMPLOYEE_ID:";
    attendanceData += String(EMPLOYEE_ID);
    attendanceData += ",RFID_UID:";
    attendanceData += uid;
    attendanceData += ",FINGERPRINT_ID:";
    attendanceData += String(expectedFingerId);

    Serial1.println(attendanceData);

    Serial.println();
    Serial.println("Sending attendance to ESP32...");
    Serial.println(attendanceData);
    Serial.println("UART message sent.");
    lcdStatus(F("ID verified"), F("Sent to ESP32"), RFID_COOLDOWN);

  } else {
    Serial.println();
    Serial.println("==============================");
    Serial.println("ATTENDANCE REJECTED!");
    Serial.println("Fingerprint verification failed.");
    Serial.println("==============================");
    lcdStatus(F("Not verified"), F("Tap RFID again"), RFID_COOLDOWN);
  }

  lastRFIDScan = millis();

  Serial.println();
  Serial.println("SYSTEM READY");
  Serial.println("Tap your RFID card.");
}

// ========================================
// FINGERPRINT VERIFICATION
// ========================================

bool verifyFingerprint() {
  unsigned long startTime = millis();
  unsigned long lastMessage = millis();
  byte captureErrors = 0;

  Serial.println("Scanning fingerprint...");
  lcdStatus(F("Scan fingerprint"), F("Place finger"));

  while (millis() - startTime < FINGER_TIMEOUT) {
    uint8_t result = finger.getImage();

    // Waiting for fingerprint
    if (result == FINGERPRINT_NOFINGER) {
      if (millis() - lastMessage >= 5000) {
        Serial.println("Waiting for finger...");
        lastMessage = millis();
      }

      delay(100);
      continue;
    }

    // Image capture error
    if (result != FINGERPRINT_OK) {
      captureErrors++;

      Serial.print("Capture error code: ");
      Serial.println(result);

      if (captureErrors >= 5) {
        Serial.println("Too many capture errors.");
        return false;
      }

      delay(500);
      continue;
    }

    Serial.println("Fingerprint image captured!");

    // Convert image to fingerprint template
    result = finger.image2Tz(1);

    if (result != FINGERPRINT_OK) {
      Serial.print("Processing error code: ");
      Serial.println(result);

      Serial.println("Remove finger and try again.");
      lcdStatus(F("Image unclear"), F("Lift then retry"));
      waitForRemoval(3000);
      lcdStatus(F("Scan fingerprint"), F("Place finger"));
      delay(300);
      continue;
    }

    Serial.println("Fingerprint processed.");
    Serial.println("Searching fingerprint database...");
    lcdStatus(F("Checking finger"), F("Please wait..."));

    result = finger.fingerSearch(1);

    if (result == FINGERPRINT_OK) {
      Serial.println();
      Serial.println("FINGERPRINT MATCHED!");

      Serial.print("Fingerprint ID: ");
      Serial.println(finger.fingerID);

      Serial.print("Confidence: ");
      Serial.println(finger.confidence);

      bool matched =
        finger.fingerID == expectedFingerId;

      if (matched) {
        Serial.println("Employee identity verified!");
        lcdStatus(F("Finger matched"), F("Remove finger"));
      } else {
        Serial.println("Wrong fingerprint ID!");
        lcdStatus(F("Wrong finger"), F("Remove finger"));
      }

      waitForRemoval(3000);
      return matched;
    }

    if (result == FINGERPRINT_NOTFOUND) {
      Serial.println("Fingerprint not recognized!");
      Serial.println("Remove finger and try again.");
      lcdStatus(F("No finger match"), F("Lift then retry"));

      waitForRemoval(3000);
      lcdStatus(F("Scan fingerprint"), F("Place finger"));
      delay(300);
      continue;
    }

    Serial.print("Fingerprint search error code: ");
    Serial.println(result);

    waitForRemoval(3000);
    delay(300);
  }

  Serial.println("Fingerprint verification timed out!");
  return false;
}

// ========================================
// WAIT FOR FINGER REMOVAL
// ========================================

void waitForRemoval(unsigned long timeout) {
  unsigned long startTime = millis();

  while (millis() - startTime < timeout) {
    uint8_t result = finger.getImage();

    if (result == FINGERPRINT_NOFINGER) {
      return;
    }

    delay(100);
  }
}
