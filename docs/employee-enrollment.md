# Employee RFID and fingerprint enrollment

Website and cloud enrollment are implemented. Both updated sketches must be uploaded before physical enrollment can work. Compilation and automated checks do not replace a real sensor test.

## Upload first

Wi-Fi persistence fix: the portal now samples its timeout clock after handling HTTP requests, preventing a newly submitted connection from being immediately treated as timed out by unsigned subtraction. Successfully connected credentials are committed together in `camp-wifi/config-v1`; legacy settings are retained and migrated on a successful connection. A timed-out connection attempt no longer clears the requirement to save credentials if a later retry succeeds. Failed flash writes retry every 10 seconds and keep the portal open. After uploading this firmware, configure Wi-Fi once and wait for `Wi-Fi connected; credentials saved.` before restarting. Next boot should print `Saved Wi-Fi loaded; reconnecting automatically.` Ordinary resets preserve these settings; erasing all flash removes them. Actual power-cycle verification must be performed on the device.

### Mega acknowledgement update

Apply `supabase/migrations/202611080005_enrollment_ack.sql` in Supabase SQL Editor and use the updated website and ESP32 firmware. ESP32 reports `waiting_for_mega` until it receives a Mega enrollment response; it no longer invents `tap_card`. It retries an unacknowledged command every 10 seconds and reports `MEGA_NOT_RESPONDING` after 30 seconds. HTTP failures are logged by status without credentials. Updated Mega firmware prints `ENROLLMENT firmware v2 ready - Serial1 RX19/TX18 at 9600.` at boot and `ENROLLMENT: command received from ESP32.` on a valid command, and sends an immediate acknowledgement before sensor preparation. Both UART directions and common GND are required. Existing queue and enrollment NVS layouts are preserved. Source/build checks cannot verify physical wiring. The migration must be applied by a project administrator; the available management account previously returned HTTP 403.

1. In Arduino IDE, open `firmware/arduino-mega/arduino-mega.ino`. Select **Arduino Mega or Mega 2560**, port **COM23**, and upload. Keep the existing RC522 and AS608 wiring.
2. Open `firmware/esp32/esp32.ino`. Select **ESP32 Dev Module**, port **COM25**, and upload. Keep the existing local `secrets.h`; never share it. Install **ArduinoJson** (tested 7.4.3) if compiling on another computer. SD backup remains disabled.
3. Confirm bidirectional UART: **Mega TX18 → level shifter → ESP32 RX16**, **ESP32 TX17 → Mega RX19**, and common GND. The second wire is required for enrollment commands; attendance previously used the other direction.
4. Open ESP32 Serial Monitor at **115200 baud** (Mega is **9600**). Follow the website prompts; LCD wiring is not required for enrollment.

## Register and enroll

ESP32 checks for enrollment jobs every 1 second, and the admin refreshes status 1 second after each status request completes. Hardware progress triggers an immediate ESP32 update. This reduces the previous 15-second idle wait; network request time still adds latency. Upload the updated ESP32 sketch and use the updated website to enable both changes. Failed ESP32 HTTP requests retain the existing 10-second retry backoff.

1. Sign into the live website and select **Employees → Add employee**.
2. Fill the name, age, position, terminal/branch and employment type. Save.
3. In that employee's directory row, select **Enroll RFID & fingerprint**, then **Start RFID & fingerprint enrollment**. The form panel appears above the directory.
4. Wait for **Tap RFID card**, then tap a NEW card within 45 seconds.
5. Place a finger within 30 seconds. Lift it completely when prompted (15-second limit), then scan the SAME finger again within 30 seconds.
6. Wait for **Enrollment completed**. Only the server's completed result activates the employee. Scan the card and matching finger normally to test attendance afterward.

Do not reuse the existing prototype card or an already enrolled finger for a new employee. They are intentionally rejected to preserve existing identities. Use another card and a different finger. A request lasts 15 minutes; closing the panel does not cancel it. Only one request can run on each terminal at a time.

## Storage and retries

The admin enrollment panel includes **Cancel enrollment**. Apply `supabase/migrations/202611080004_cancel_enrollment.sql` before using the updated website. Cancellation releases the server request and prevents late results from activating the employee; it preserves completed enrollments. Existing firmware acknowledges cancellation as a failed job with `CANCELLED`. It does not immediately interrupt an ongoing sensor capture or delete saved templates/mappings. Wait for Mega `SYSTEM READY` before retrying. The migration passed local authorization, retry, and late-result tests, but remote application was denied with HTTP 403 for the available Supabase CLI account; live deployment remains pending.

- The sensor retains fingerprint templates. Cloud storage contains the template ID and card UID, never the fingerprint image or template bytes.
- Mega checks the sensor occupancy bitmap (command `0x1F`) and writes only a free slot numbered 2 or above; slot 1 and the original test card remain unchanged. Unsupported sensor replies stop enrollment with `NO_SAFE_SENSOR_SLOT`.
- Up to 40 additional mappings are stored in Mega EEPROM with checksums and a commit marker written last. Existing EEPROM data is not erased. EEPROM mapping persistence is separate from the ESP32 attendance queue.
- ESP32 uses a separate NVS namespace for enrollment commands and results. It retains the same job ID across retries and resets; Mega replays a completed local mapping instead of enrolling it twice. The existing attendance queue binary layout stays unchanged.
- The server authenticates the device token, restricts manager requests by branch, rejects duplicate card/template mappings, and atomically activates the employee. Repeated matching results are acknowledged without creating another employee.
- If power fails between sensor storage and EEPROM commit, an unused sensor template may remain. If a cloud conflict or expiry occurs after local storage, the local mapping may remain reserved. No templates are automatically erased. An administrator must reconcile that case; do not reset the whole sensor or reuse that card for another employee.

## Verification

`node --test functions/hardware-enrollment.test.cjs functions/employee-registration.test.cjs functions/supabase-schema.test.cjs`

Checks include branch access, device isolation, activation, duplicate replies, mapping conflicts, expiry, original test-template preservation, and attendance acceptance after enrollment. Live negative probes: `node functions/verify-enrollment-live.cjs`. Hardware capture still needs testing after the user uploads both sketches.

Sensor occupancy protocol reference: [Adafruit Fingerprint read_templates implementation](https://github.com/adafruit/Adafruit_CircuitPython_Fingerprint/blob/main/adafruit_fingerprint.py).
