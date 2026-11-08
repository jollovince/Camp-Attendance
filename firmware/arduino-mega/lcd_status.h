#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Confirmed display: 16 columns x 2 rows, standard PCF8574 I2C backpack.
// Mega SDA=20, SCL=21. Set an explicit address only if several expanders exist.
constexpr uint8_t LCD_COLUMNS = 16;
constexpr uint8_t LCD_ROWS = 2;
constexpr uint8_t LCD_ADDRESS_OVERRIDE = 0; // 0 means detect exactly one candidate.
LiquidCrystal_I2C *statusLcd = nullptr;
unsigned long lcdReturnAt = 0;

void lcdStatus(const __FlashStringHelper *first, const __FlashStringHelper *second, unsigned long holdMs = 0) {
  lcdReturnAt = holdMs ? millis() + holdMs : 0;
  if (!statusLcd) return;
  statusLcd->setCursor(0, 0);
  statusLcd->print(F("                "));
  statusLcd->setCursor(0, 0);
  statusLcd->print(first);
  statusLcd->setCursor(0, 1);
  statusLcd->print(F("                "));
  statusLcd->setCursor(0, 1);
  statusLcd->print(second);
#if defined(WIRE_HAS_TIMEOUT)
  if (Wire.getWireTimeoutFlag()) {
    statusLcd = nullptr;
    Wire.clearWireTimeoutFlag();
    Serial.println(F("LCD disabled after I2C timeout; serial verification remains available."));
  }
#endif
}

void beginStatusLcd() {
  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(25000, true);
#endif
  uint8_t foundAddress = 0;
  uint8_t candidates = 0;
  for (uint8_t address = 0x20; address <= 0x3F; address++) {
    if (LCD_ADDRESS_OVERRIDE && address != LCD_ADDRESS_OVERRIDE) continue;
    if (address > 0x27 && address < 0x38) continue;
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("I2C backpack candidate: 0x"));
      Serial.println(address, HEX);
      foundAddress = address;
      candidates++;
    }
  }
  if (candidates != 1) {
    Serial.println(candidates ? F("Multiple I2C backpacks: set LCD_ADDRESS_OVERRIDE. LCD disabled.") : F("LCD not detected. Check Mega SDA20/SCL21, power and wiring."));
    return;
  }
  static LiquidCrystal_I2C detectedLcd(foundAddress, LCD_COLUMNS, LCD_ROWS);
  statusLcd = &detectedLcd;
  statusLcd->init();
  statusLcd->backlight();
  lcdStatus(F("Camp of Coffee"), F("Starting..."));
}

void showLcdReady() {
  lcdStatus(F("Camp of Coffee"), F("Tap RFID card"));
}

void refreshLcdReady() {
  if (lcdReturnAt && (long)(millis() - lcdReturnAt) >= 0) showLcdReady();
}
