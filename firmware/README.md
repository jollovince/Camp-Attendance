# Hardware baseline

## ESP32 Wi-Fi hotspot fallback

The ESP32 tries the saved Wi-Fi credentials (or `secrets.h` on first boot). After 30 seconds without a connection, it starts `Camp-Setup-XXXX` with password `campwifi123`. Connect a phone or laptop to that hotspot and open `http://192.168.4.1/` if the setup page does not appear automatically. Stay connected even if the phone reports no internet. The setup page automatically scans available 2.4 GHz networks. Select a network from the list and enter its password; use Refresh Wi-Fi to scan again, or type the name manually for a hidden network. Scanning runs asynchronously so the attendance loop can continue. Credentials are saved in a separate NVS namespace only after successful connection; the hotspot closes 15 seconds later. Failed attempts leave the setup page available, and losing Wi-Fi later activates the fallback again. Attendance buffering continues while offline, subject to the existing 20-event queue limit. The hotspot provides Wi-Fi setup, not internet sharing. Upload the updated ESP32 sketch to enable this behavior; physical connection testing is still required.

## Website-driven employee enrollment

Both sketches now implement website-driven RFID and two-capture fingerprint enrollment. Upload the updated Mega sketch to COM23 and ESP32 sketch to COM25. This firmware update has not yet been uploaded or physically tested; the user performs the uploads. EEPROM stores up to 40 additional card/template mappings, preserving the original test identity. ESP32 enrollment uses a separate NVS namespace and leaves the attendance queue format intact. See [step-by-step enrollment instructions](../docs/employee-enrollment.md).


## LCD addition

The Mega workspace sketch now includes 16x2 I2C LCD prompts via `lcd_status.h`. It compiled successfully for `arduino:avr:mega` (16,510 bytes flash, 1,932 bytes global RAM). Physical wiring and display output are not yet tested; the user is still wiring the display. SDA is Mega pin 20, SCL pin 21, with 5 V and GND for the standard backpack. The pre-LCD sketch is backed up in ignored `backups/arduino-mega-before-lcd.ino` and matches the original supplied file's SHA-256. RFID/fingerprint mapping and UART payload remain unchanged. See [LCD wiring and test steps](../docs/lcd-setup.md).

## Current bridge

microSD CSV journaling is deferred by user request and disabled in source (`CAMP_SD_BACKUP_ENABLED=0`). The user chose to upload the updated sketch themselves. With this build, missing SD hardware cannot block cloud upload; the persistent 20-event NVS queue and Supabase transport remain enabled. The no-SD startup message is `SD backup disabled. Using persistent ESP32 flash queue and cloud upload.` Actual upload verification remains pending. The optional SD implementation is retained for later work; see [microSD notes](../docs/microsd-backup.md).

`esp32/esp32.ino` implements UART reception, a bounded durable NVS queue and authenticated verified-HTTPS upload. It compiled successfully using ESP32 core 3.3.12 and was uploaded to the ESP32-D0WD-V3 on COM25, with flash hashes verified. Serial boot confirmed `ESP32 ATTENDANCE BRIDGE READY`. The real Supabase endpoint and terminal credential are configured in ignored `secrets.h`. One actual Mega card/fingerprint scan reached Supabase at 14:16:37 Asia/Manila on November 8, 2026; identical replay returned the existing receipt without another record. Firebase handles administrator login/hosting; see `../docs/hybrid-setup.md`. A full pre-upload flash backup is retained locally in ignored `backups/`. Offline/power-loss recovery and physical rejection cases remain untested.

The remainder records the imported baseline before this bridge implementation.

The original files outside this workspace were not modified. `arduino-mega/arduino-mega.ino` is an exact copy of the supplied Mega sketch. `esp32/esp32.ino` preserves the supplied Wi-Fi test, replacing its embedded credentials with an ignored `secrets.h`. Copy `secrets.example.h` to `secrets.h` and fill it locally before compiling.

Mega libraries found locally: MFRC522, Adafruit Fingerprint Sensor Library, Adafruit BusIO. Arduino CLI was not found; firmware has not been compiled or tested on hardware in this session.

Verified baseline wiring from the handoff:

| Module | Mega pins |
|---|---|
| RC522 SS / SCK / MOSI / MISO / RST | 53 / 52 / 51 / 50 / 5 |
| AS608 TX / RX | RX2 17 / TX2 16, 57600 baud |
| ESP32 RX GPIO16 / TX GPIO17 | TX1 18 / RX1 19, 9600 baud |

Use common ground and appropriate 5 V-to-3.3 V level shifting to RC522 inputs and ESP32 RX. Preserve the existing verified sensor supply.

Mega emits `ATTENDANCE_VERIFIED,EMPLOYEE_ID:1,RFID_UID:A3 F0 70 33,FINGERPRINT_ID:1` after RFID then fingerprint verification. The supplied ESP32 sketch only tests Wi-Fi; it does not receive UART or send events. Network firmware is a subsequent milestone.
