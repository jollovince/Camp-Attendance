# microSD CSV backup preparation

**Deferred by user request:** SD mounting failed on the actual setup. The user asked to focus on other work, so `CAMP_SD_BACKUP_ENABLED` is now `0`. They will upload the updated sketch themselves. That firmware uses the existing persistent NVS queue and uploads to Supabase without requiring an SD card. Card contents are not modified or formatted. The enabled-mode procedure below is retained for a future milestone.

The user supplied a six-pin SPI reader (CS, clock, MOSI, MISO, VCC, GND) and confirmed a 32 GB card. Module supply voltage, card filesystem, and physical wiring are not yet verified. Do not power or upload the SD-enabled firmware until those details are confirmed.

## Proposed ESP32 signals

| Module label | ESP32 GPIO |
|---|---|
| CS | 13 |
| SCK / CLK (reported as SCX; verify silkscreen) | 18 |
| MOSI | 23 |
| MISO | 19 |
| GND | Common GND |
| VCC | Pending exact module input-voltage confirmation |

GPIO16/17 remain dedicated to Mega UART. The SPI numbers above are ESP32 pins, not the Mega's pins. Turn off power before wiring. A bare card uses 3.3 V, while regulated carrier boards may require another input; the image alone is not a verified module datasheet. Confirm that the module's MISO output is safe for 3.3 V ESP32 inputs.

Use an already FAT32-formatted card. Formatting erases files; no formatting has been performed and the firmware never automatically formats cards.

The implementation uses the installed ESP32 core's [SD/SPI API](https://github.com/espressif/arduino-esp32/blob/3.3.12/libraries/SD/examples/SD_Test/SD_Test.ino). Filesystem power-loss behavior must be tested on the actual card; [Espressif's FAT filesystem documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/fatfs.html) describes the storage stack.

## Prepared software behavior

The user has now confirmed a 3.3 V module and completed wiring/card installation. `firmware/esp32/sd_config.h` is set to `CAMP_SD_BACKUP_ENABLED 1`. Firmware upload and card mounting still await COM25 being released by Arduino Serial Monitor. The Mega remains unchanged.

The SD-enabled variant compiled successfully against installed ESP32 core 3.3.12: 1,154,308 bytes program storage (88%) and 51,416 bytes global RAM (15%). Physical mounting, CSV contents and power-loss recovery have not been tested. This compile used an isolated build directory and did not upload firmware.

When enabled:

1. The existing 20-event NVS queue is saved first; its binary layout/version is preserved.
2. The ESP32 appends a `captured` entry to `/attendance.csv` and flushes, closes, then reads the appended bytes back. If the clock is not synchronized, its timestamp field is blank.
3. Pending events recovered after reboot append `pending_recovered` before upload. The timestamp here is recovery/logging time, not the original scan time.
4. After a successful server receipt, it appends `server_confirmed` before removing the event from NVS.
5. If the card is missing, full, or a write fails, cloud transmission/dequeue pauses and the NVS queue is retained. Mount retries occur every 10 seconds. At 20 queued events, further scans are rejected with a queue-full message; SD is a backup journal, not an unlimited upload queue.

CSV columns: `event_id,device_id,rfid_uid,fingerprint_template_id,record_type,recorded_at_utc`. It is a raw verification/delivery journal, not a time-in/time-out or payroll report. `server_confirmed` records the device's acknowledgement time, not the authoritative server reception timestamp. Template references are included; biometric images/templates and device credentials are never written to CSV.

For a read-only check, send `SD_STATUS` followed by a newline to the ESP32 USB serial port at 115200 baud. It reports card capacity and the last 2048 bytes of the actual CSV between `SD_CSV_BEGIN` and `SD_CSV_END`. This command does not create a scan, format, delete, or import data.

The same event can have several journal entries and replay/recovery can repeat entries. Count distinct event IDs, not CSV lines. Server idempotency remains unchanged. When restoring data, do not trust a CSV as independent proof of identity; reconciliation/import is not implemented.

Existing CSV files are appended to. A partial final row after interruption is preserved and separated before the next append; verify that damaged row manually. Readback plus flush reduces risk but cannot make FAT writes immune to sudden power loss or SD corruption. Previously written data normally persists without power. The terminal needs a battery/UPS to continue scanning during an outage; a microSD reader does not supply power. An RTC would be needed for a reliable absolute scan time after a cold boot without network time.

## Required physical verification

Confirm supply/wiring and FAT32 first. Then check mounting, one real scan's captured/confirmed entries, matching event ID in Supabase, offline queue restoration, full/missing card behavior, and controlled power cycling. Back up card contents before power-loss tests. Do not remove the card while writes are active.
