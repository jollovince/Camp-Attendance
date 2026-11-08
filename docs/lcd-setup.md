# 16x2 I2C attendance display

The user confirmed a 16x2 display with an I2C backpack and will wire it to the Arduino Mega. Physical wiring/display operation have not yet been verified. SD work remains deferred; ESP32 firmware is separate and keeps its no-SD configuration.

Power off before wiring:

| LCD backpack | Arduino Mega 2560 |
|---|---|
| GND | GND |
| VCC | 5 V (standard 5 V LCD/backpack) |
| SDA | 20 / SDA |
| SCL | 21 / SCL |

Use the Mega, not the ESP32, for this 5 V I2C backpack. Existing Mega Serial1 pins 18/19, fingerprint Serial2 pins 16/17 and RC522 SPI pins remain unchanged. [Arduino Mega documentation](https://docs.arduino.cc/hardware/mega-2560).

Installed library: `LiquidCrystal I2C` 1.1.2 (Frank de Brabander / Marco Schwartz), AVR architecture. The sketch detects exactly one acknowledging PCF8574/PCF8574A candidate in 0x20–0x27 or 0x38–0x3F; it does not assume 0x27. If none or several respond, LCD is disabled and Serial operation continues. Multiple candidates require setting `LCD_ADDRESS_OVERRIDE` in `lcd_status.h` after verifying which is the LCD. A responding expander still requires a compatible standard LCD pin mapping; detection alone does not prove visible text.

Compile verification passed for `arduino:avr:mega` using installed AVR core 1.8.8: 16,510 bytes flash (6%) and 1,932 bytes global RAM (23%). No firmware upload, actual address detection or physical display check has been performed for this change.

The LCD shows startup, idle RFID prompt, unregistered card, fingerprint capture/retry, match or rejection, and `Sent to ESP32` after the existing UART send. It never labels UART transmission as a server save or time-in/time-out. Success/error messages return to the ready screen during the existing 2-second cooldown, without adding blocking result delays.

The current Mega sketch is backed up in ignored `firmware/backups/arduino-mega-before-lcd.ino`. Original files outside the workspace remain unchanged. Upload `firmware/arduino-mega/arduino-mega.ino` with its adjacent `lcd_status.h` to the Mega (previously COM23), not to the ESP32. Serial Monitor is 9600 baud.

After upload, check the detected address on Serial Monitor, adjust the blue contrast trimmer gently if backlight is on but text is invisible, then verify: idle prompt, wrong card rejection, valid card/finger prompt, matching fingerprint, UART send, and the corresponding Supabase scan. No fingerprint reset or enrollment is performed.
