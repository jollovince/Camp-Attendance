# Camp of Coffee Employee Attendance Monitoring System

> **Project handoff for Codex / VS Code**  
> Updated: November 8, 2026  
> Status: Hardware proof of concept is working; Firebase integration and web dashboard are **planned, not yet implemented**.

## 1. Read this first (Codex instructions)

You are helping redevelop an existing capstone prototype into a maintainable employee attendance monitoring system for **Camp of Coffee**. Read this file before changing code. Inspect the actual repository and installed hardware libraries before proposing edits. **Do not claim that a feature or test has been completed unless it was actually implemented and tested.** Work in small, independently verifiable stages, retain working functionality, and report changed files and test results.

User preferences for the project:
- Code, filenames, technical comments, and programming explanations: **English**.
- Keep implementation instructions clear and incremental; avoid overwhelming the user with unrelated tasks.
- Preserve working Arduino RFID/fingerprint integrations, their verified wiring, and the established RFID-first sequence.
- For website UI, avoid generic AI-looking dashboard layouts. Use consistent typography, meaningful hierarchy, contrast, sensible spacing, responsive tables, keyboard access, and clear form validation. Keep chosen branding consistent.
- Do **not** commit secrets, Firebase service account JSON, private keys, access tokens, Wi-Fi passwords, real biometric templates, or actual personal data.

**Source of truth:** This file summarizes prior discussion and the supplied May 2026 capstone draft, *Development of an Employee Tracking System Utilizing Fingerprint Recognition and RFID Technology for Camp of Coffee* (Aquino, Navarra, Tambobong). It is a handoff, not a verified representation of repository contents. Confirm implementation details from actual code.

## 2. Title, goals, scope

**Original paper title:** Development of an Employee Tracking System Utilizing Fingerprint Recognition and RFID Technology for Camp of Coffee.

**Purpose:** Replace paper-based time-in/time-out attendance with an RFID-then-fingerprint verification device and a centralized attendance system. Reduce manual data-entry mistakes and discourage proxy attendance/buddy punching.

**Confirmed paper scope:**
- Three Camp of Coffee branches using a centralized database.
- Employee registration, RFID assignment, fingerprint enrollment/reference, real-time attendance monitoring, time-in/time-out, attendance records, and daily/weekly/monthly reporting.
- Roles mentioned in the paper: owner, team leader, administrator, employees; HR/accounting are beneficiaries but not necessarily separate app roles.
- Full-time and part-time employees.
- **Excluded:** payroll computation/payment processing, leave management, inventory, and customer transactions. Attendance reports may help manual payroll preparation, but the system must not claim to calculate payroll.

**Project decision made after writing the paper:** Use **Firebase** instead of PHP + MySQL + AdminLTE as the main backend/database plan. The paper's originally documented stack and architecture therefore need formal updating before final submission. Do not silently modify academic chapters without asking.

## 3. Confirmed progress (actually observed)

| Component/test | State | Evidence from interaction |
|---|---|---|
| Arduino Mega 2560 | Working | Executes RFID + fingerprint integrated sketch |
| Two RC522 readers | Previously both worked together | Later design decision: use **one** RC522 only |
| One RC522 | Working | Diagnostic output `Firmware Version: 0x92 = v2.0`; card UID read |
| AS608 optical fingerprint sensor | Working | Detected with Adafruit library; initial capacity 300, 13 templates |
| AS608 database reset | User reported completed | Former templates deleted; do not repeat reset |
| AS608 enrollment | Working | Fingerprint registered at template ID `1` |
| AS608 matching | Working | `FINGERPRINT MATCHED!`, ID `1`, confidence reported `86` (not 86% accuracy) |
| RFID → fingerprint verification | Working | Successful test using a registered card and fingerprint |
| Arduino Mega UART → ESP32 | Working | ESP32 printed received attendance event from Mega |
| ESP32 firmware upload and boot | Working | `ESP32 READY! Waiting for Arduino Mega...` |
| ESP32 Wi-Fi | User confirmed connected | SSID/password must not be stored in repo |
| Firebase project / Firestore / backend / web dashboard | **Not yet confirmed implemented** | These are the next development phases |

**Latest registered test credentials (not real identity assertions):**
- RFID UID: `A3 F0 70 33` (most recent working single-reader card).
- AS608 fingerprint template ID: `1`.
- Arduino-side test employee ID: `1`.
- Earlier UID `A3 3F A1 39` was used with the previous two-reader setup and is **not** the latest registered UID.
- Hardware credentials are only a **prototype test mapping**. UID alone is cloneable and should not be treated as cryptographic security.

## 4. Confirmed hardware and wiring

### 4.1 Current hardware

- Arduino Mega 2560: reads RFID, verifies fingerprint, emits a UART attendance event.
- **One** MFRC522 / RC522 reader (not two in the final chosen configuration).
- AS608 fingerprint scanner with six-wire connector; only the verified power and UART signals are used for the current implementation.
- ESP32 development board: receives UART and will communicate to backend over Wi-Fi.
- Separate USB connections used for development/program upload; normal operation requires suitable regulated supplies.
- LCD, LEDs, buzzer, enclosure, optional offline storage appear in the paper's prototype, but their **actual connected status and part specifications are not confirmed**.

### 4.2 RC522 to Arduino Mega 2560

| RC522 | Mega |
|---|---|
| SDA/SS | 53 |
| SCK | 52 |
| MOSI | 51 |
| MISO | 50 |
| RST | 5 |
| VCC | Regulated 3.3 V |
| GND | Common GND |
| IRQ | Not connected |

**Electrical safety:** Mega outputs are 5 V logic; RC522 is a 3.3 V device. Use appropriate 5 V-to-3.3 V level shifting on Mega-to-RC522 signals (SS/SCK/MOSI/RST); do not feed 5 V into the RC522 supply. Ensure available power/current and common grounding.

### 4.3 AS608 to Arduino Mega 2560

| AS608 | Mega |
|---|---|
| TX | RX2, pin 17 |
| RX | TX2, pin 16 (respect sensor UART logic voltage) |
| VCC | Existing working, verified sensor supply; exact model variant rating must be checked before rewiring |
| GND | Common GND |

The AS608 was successfully tested with `Adafruit_Fingerprint finger(&Serial2)` and `finger.begin(57600)`. The serial pins on the Mega are not the same as pins for ESP32 communication. Unused auxiliary wires must not be connected without a confirmed pinout.

### 4.4 Arduino Mega to ESP32 UART

| Mega | ESP32 (for dev board with these pins) |
|---|---|
| TX1, pin 18 | RX2 / GPIO16 through **5 V-to-3.3 V** level shifting |
| RX1, pin 19 | TX2 / GPIO17 (3.3 V output is normally readable as HIGH by Mega) |
| GND | GND |

Mega uses `Serial1.begin(9600)` to transmit. ESP32 uses `HardwareSerial MegaSerial(2)` and `MegaSerial.begin(9600, SERIAL_8N1, 16, 17)`. The specific ESP32 board's pinout must be checked; GPIO16/17 are not available in all variants. Do not connect the Mega's 5 V TX directly to ESP32 RX. Never tie unrelated powered rails together without a designed power supply.

## 5. Existing firmware behavior

**Arduino libraries:** `SPI.h`, `MFRC522.h`, `Adafruit_Fingerprint.h`.

Working proof-of-concept process:
1. Read RFID UID.
2. Compare it against test UID `A3 F0 70 33`.
3. If card matches, allow 30 seconds for AS608 scan.
4. Call `getImage()`, `image2Tz(1)`, `fingerFastSearch()`.
5. Require returned fingerprint ID `1`.
6. On success, print `ATTENDANCE VERIFIED!` to Mega Serial Monitor.
7. Write event string to `Serial1` for ESP32.

**Actual UART payload observed on ESP32:**

```text
ATTENDANCE_VERIFIED,EMPLOYEE_ID:1,RFID_UID:A3 F0 70 33,FINGERPRINT_ID:1
```

ESP32 previously displayed:

```text
ESP32 READY!
Waiting for Arduino Mega...
Received from Mega: ATTENDANCE_VERIFIED,EMPLOYEE_ID:1,RFID_UID:A3 F0 70 33,FINGERPRINT_ID:1
```

Do not assume these two firmware sketches are present in the repository. Ask Codex to inspect/import the currently working `.ino` files (or provide them to Codex) before changing firmware. If implementing backend event transport, prefer a versioned, well-defined message format, message IDs, checksums if needed, acknowledgements, duplicate handling, and retries. Preserve compatibility or migrate both sides together.

**Important:** `ATTENDANCE_VERIFIED` is hardware authentication only, **not proof that a server record was created**. Time-in/time-out categorization should happen under explicit shift/attendance business rules at the backend, not from the UART message alone.

## 6. Planned Firebase architecture

Preferred overall flow:

```text
Employee
   |
   v
RC522 card scan -> AS608 fingerprint verification
   |
   v
Arduino Mega 2560 -- UART (Serial1) --> ESP32
                                           |
                                           v
                                       Wi-Fi HTTPS
                                           |
                                           v
                               Authenticated server endpoint
                           (e.g. Firebase Cloud Function)
                                           |
                                           v
                                  Cloud Firestore
                                           |
                                           v
                            Web admin dashboard (real time)
```

**Recommended Firebase components** (planned):
- **Cloud Firestore:** centralized employee profiles, branch assignments, RFID mappings, fingerprint *template references*, shifts and attendance event records.
- **Firebase Authentication:** admin/owner/team-leader sign-in. Define precise roles and branch-scoped access.
- **Cloud Functions for Firebase or an equivalent secured API:** accept device-sent attendance events, authenticate device, verify message format, deduplicate, assign authoritative server timestamps, enforce business rules, write Firestore in transactions where appropriate.
- **Firebase Hosting:** host the web dashboard (optional if another hosting solution chosen).

**Device security:** Do not embed Admin SDK service-account private keys into ESP32 firmware or expose Firestore write access publicly. Use an authenticated HTTPS ingestion endpoint with a per-device credential strategy and revocation. TLS, device identity, input validation, replay protection, and restricted API access are required before production use. Restrict Firestore security rules so an ordinary web client cannot create or arbitrarily edit verified attendance logs. Check deployment/billing requirements for chosen Firebase functions before committing to an implementation.

**Biometrics:** For the prototype, keep AS608 biometric templates in the sensor's internal storage and store only `fingerprintTemplateId`/metadata in Firestore. Do **not** upload raw fingerprints or templates to Firestore by default. Enrollment, replacement, deletion and reassignment of templates must be controlled and audited. Plan privacy notices, lawful processing, retention/deletion and employee access controls consistent with applicable Philippine data-privacy requirements.

## 7. Proposed Firestore data model (design, not implemented)

Use collection and field names consistently. These are recommended starting points; refine before writing production schema.

### `branches/{branchId}`

```json
{
  "name": "Camp of Coffee - Branch 1",
  "code": "BR-001",
  "isActive": true,
  "createdAt": "server timestamp"
}
```

### `employees/{employeeId}`

```json
{
  "employeeCode": "EMP-0001",
  "fullName": "Sample Employee",
  "branchId": "branch-1",
  "employmentType": "full_time",
  "status": "active",
  "rfidUid": "A3 F0 70 33",
  "fingerprintTemplateId": 1,
  "assignedDeviceId": "device-1",
  "createdAt": "server timestamp",
  "updatedAt": "server timestamp"
}
```

This example uses the existing test UID/template and a **fictional** name. A template ID can collide across sensors, so identity references must include sensor/device context. UID is an identifier, not a secure authentication secret.

### `devices/{deviceId}`

```json
{
  "name": "Attendance Terminal 1",
  "branchId": "branch-1",
  "status": "active",
  "lastSeenAt": "server timestamp",
  "firmwareVersion": "prototype"
}
```

### `attendanceEvents/{eventId}` (immutable raw events)

```json
{
  "eventId": "device-1:unique-event-id",
  "deviceId": "device-1",
  "branchId": "branch-1",
  "employeeId": "employee-1",
  "rfidUid": "A3 F0 70 33",
  "fingerprintTemplateId": 1,
  "verificationMethod": "rfid_and_fingerprint",
  "deviceCapturedAt": "optional device timestamp",
  "receivedAt": "server timestamp",
  "result": "verified",
  "attendanceAction": "time_in"
}
```

Never trust client-supplied employee/branch mapping blindly. Resolve and validate server-side against registered device/credentials. `attendanceAction` is decided under shift and policy rules and can be omitted until implemented. Use `eventId` for idempotency.

### Additional collections (after requirements approval)

- `users/{uid}`: authenticated web-user profile, `role`, allowed `branchIds`.
- `shifts/{shiftId}` or `schedules/{scheduleId}`: shift start/end, grace periods, overnight handling, branch.
- `dailyAttendance/{employeeId_date}`: derived summaries / time-in/time-out as needed.
- `auditLogs/{logId}`: admin corrections, enrollment changes, sensitive operations.

## 8. Web admin dashboard requirements

Build a **responsive, accessible, production-minded Camp of Coffee attendance dashboard**, designed for desktop first and usable on tablet/mobile. This is a **new planned application**, not a confirmed existing implementation.

### Navigation and screens

1. **Login**: Firebase Auth; secure session handling; errors, loading states; no public signup for admin roles unless specifically approved.
2. **Overview dashboard**: branch filter, date filter, employees on shift (if schedules are implemented), today’s verified time-ins/time-outs, late/missing attendance (only when rules exist), recent scans, reader/device health, and clear data freshness state.
3. **Live Attendance**: Firestore listener for validated events, employee name, branch, type, timestamp, status, device; loading, empty and failure states; avoid exposing unnecessary biometric details.
4. **Employees**: search/filter, add/edit/deactivate employees, branch assignments, employee codes, card registration, fingerprint template linkage, enrollment status. Restrict destructive actions and audit changes.
5. **Schedules / Shifts**: define operating/employee shifts and rules required to categorize events; if not yet in scope, show as future phase rather than fake working metrics.
6. **Daily Time Records (DTR)**: filter by employee, branch, date range; exact time-in/time-out, missing scans, corrected entries, annotations and correction history.
7. **Reports**: daily/weekly/monthly attendance, by branch and employee, CSV export; PDF export optional. This supports payroll preparation but **does not compute payroll**.
8. **Devices**: registered ESP32 terminals, branch assignment, last-seen heartbeat, online/offline indication derived from timestamps, firmware info, status/error logs.
9. **Users & Roles**: owner/admin/team-leader access permissions and branch restrictions; lock down security rules on backend, not just hide buttons.
10. **Settings / Audit Logs**: non-sensitive project configuration, attendance rules, audit trail and privacy controls.

### Role model (proposed, clarify before production)

| Role | Recommended permissions |
|---|---|
| Owner / super admin | Cross-branch read/admin, approve critical changes |
| Admin | Manage assigned branches, staff, devices, reports as granted |
| Team leader / branch manager | View own branch attendance, limited employee actions, request/approve corrections if policy allows |
| Employee | No web admin access by default; optional employee self-service is future scope |

### UI/UX direction

- Brand: **Camp of Coffee**; use warm coffee-inspired neutrals, a deep espresso/navy text color, restrained warm accent, and readable contrast. Exact official colors/logo are **not provided**; request brand assets or define documented provisional tokens.
- Avoid overdecorated gradients, indiscriminate cards, random iconography, generic AI-generated marketing copy, and fake KPIs.
- Prioritize a coherent navigation rail/sidebar, clear header, useful filter controls, legible data tables, and visual attention on attendance status.
- Display precise branch-local times in `Asia/Manila`; store server timestamps as Firebase timestamps (UTC representation), not formatted strings.
- Use Plus Jakarta Sans only if suitable and licensed/imported; otherwise use a high-quality fallback. Avoid assuming an existing font choice.
- Responsive behavior, keyboard/focus states, labels, appropriate colors for errors/status, actionable error messages, empty states, form validation, confirmations for destructive actions.
- Base design review on Vercel Web Interface Guidelines and the user's preferred Taste Skill, Impeccable and VoltAgent design references. If configured, use Playwright MCP for browser validation of navigation, forms, responsive layout and console errors; do not claim test coverage without running it.

### Suggested web stack (proposal)

- React + TypeScript + Vite (or Next.js if justified), Firebase Web SDK, Firestore and Authentication.
- UI: CSS variables / scoped styles or a well-structured Tailwind configuration; choose one coherent approach.
- Charts only where useful and supported by real data; never display invented metrics.
- Firebase Hosting or suitable secure hosting.
- Firebase Cloud Functions with TypeScript/Node.js for privileged backend processing. Firestore rules and indexes version-controlled.

The paper's legacy HTML/AdminLTE/PHP/MySQL stack is **superseded by user preference**, but document the change in the capstone write-up.

## 9. Attendance and verification business rules to define

- Exact RFID-first, then fingerprint sequence, timeout, retries and appropriate errors.
- Enforce **same employee** mapping for the RFID and fingerprint template on the **correct terminal/branch**.
- Decide whether two RC522 readers are necessary: current choice **one RFID only**.
- Decide how time-in versus time-out is determined (manual selection or backend shift/state logic). This is **not yet finalized**.
- Prevent accidental duplicate scans; idempotency on server, cool-down on firmware; never lose intentional distinct events (breaks, shifts).
- Grace period, absences, lateness, missed scans, overnight shifts, multiple entries, interbranch staff movement and manual adjustments.
- Offline behavior: cache queued verified events with durable event IDs if hardware supports reliable storage; reconcile safely after reconnect. Offline storage is **planned, not confirmed implemented**.
- Server time is authoritative for reception, while client timestamps, if available, are annotated and validated.
- An RC522 UID can be cloned, so avoid promising complete prevention of buddy punching; combined fingerprint verification helps reduce risk.

## 10. Recommended delivery plan

### Phase A: Preserve baseline
- Import and back up the two currently working Arduino/ESP32 `.ino` sketches.
- Put working pin mappings and required libraries in `firmware/README.md`.
- Confirm firmware still passes RFID → fingerprint → UART after refactoring.

### Phase B: Firebase foundation
- Create Firebase project and Firestore database; choose region deliberately.
- Set up Firebase Auth, role model, Firestore rules and indexes.
- Use Firebase Emulator Suite for secure local testing where practical.
- Store Firebase client configuration separately from server-only secrets.

### Phase C: Secure ingestion API
- Create Cloud Function / secure HTTPS endpoint.
- Authenticate ESP32 terminal, validate payload, look up employee matching, deduplicate event ID, write event with server timestamp, return receipt.
- Test endpoint with a local HTTP client before involving ESP32.

### Phase D: ESP32 network integration
- Preserve UART receiver.
- Parse / validate serial messages and POST to HTTPS endpoint.
- Add Wi-Fi reconnect, HTTP timeouts, response logging, retries, bounded buffer and unique event IDs.
- Avoid storing private service-account credentials on the board.

### Phase E: Website
- Build app shell, login, role guard and branch filter.
- Build Firestore-driven live attendance and employee screens.
- Add schedules/DTR/reporting after agreeing on time-in/time-out rules.
- Add audit logs, device status and admin correction flows.

### Phase F: Testing and deployment
- End-to-end hardware → ESP32 → API → Firestore → dashboard.
- Cases: wrong card, wrong fingerprint, lost Wi-Fi, server errors, duplicate event, unregistered device, revoked credentials, missing clock-out, branch access violations.
- Validate Firebase rules, time zones, accurate reporting and privacy controls.
- Document implementation and update capstone technical background, architecture and scope accordingly.

## 11. Test acceptance criteria

1. Valid card + matching fingerprint ID: Arduino emits one verified event, ESP32 receives it, server stores it once, website displays it.
2. Unknown card: no fingerprint success accepted; no verified attendance stored.
3. Registered card + wrong fingerprint: attendance rejected and not stored as verified.
4. Invalid/replayed device request: server rejects unauthorized/repeated events.
5. Same event retransmitted after network failure: one canonical Firestore event.
6. Owner sees allowed branches; branch manager cannot read/write other branches.
7. Timestamp display is consistent in Philippine local time and supported by server-side data.
8. Reports match actual stored events, and corrections retain audit history.

**Current passing tests only cover the hardware path up to ESP32 UART reception and ESP32 Wi-Fi association.** Do not mark criteria 1–8 fully complete until Firebase/web integration is tested.

## 12. What Codex should do next

1. Inspect the existing workspace. Report what code/files exist and what is missing; do not overwrite working firmware.
2. Propose a lean monorepo layout such as:

```text
camp-of-coffee/
  PROJECT_CONTEXT.md
  firmware/
    arduino-mega/
    esp32/
  web/
  functions/
  firebase.json
  firestore.rules
  firestore.indexes.json
  docs/
    hardware-wiring.md
    architecture.md
    test-plan.md
```

3. Start with Firebase project scaffolding, Firestore schema/rules and the secure ingestion endpoint **only after confirming project configuration**. Do not invent project IDs, API URLs, credentials or deployed resources.
4. Build the web dashboard with realistic sample data in an explicitly labeled local demo mode until real Firebase data is connected. Clearly distinguish UI mock data from production data.
5. Work one milestone at a time. Explain prerequisites and provide exact commands and verification steps for Windows + VS Code.

## 13. Open questions / unverified details

- Firebase project ID, billing status, region and actual console setup are unknown.
- Exact ESP32 board variant is unconfirmed (but ESP32 serial/UART and Wi-Fi were successfully tested).
- Actual repo location and source files are not supplied to Codex by this context file.
- The current connected physical state of LCD/LED/buzzer and any backup storage is unknown.
- Brand logo, approved fonts, official color palette and company photos are not yet provided.
- Final attendance action rules, shift policy, employee roles and operating branch names are not yet agreed.
- Whether real employees consented to fingerprint processing and how their privacy notices/retention work is not established.

---

**Suggested first Codex prompt:**

> Read `PROJECT_CONTEXT.md` completely and inspect the current repository. We are rebuilding the Camp of Coffee employee attendance system with Arduino Mega 2560, one RC522, AS608, ESP32 and Firebase. The RFID → fingerprint → UART hardware proof of concept is working; ESP32 Wi-Fi works. Do not overwrite the firmware. First propose a safe workspace structure and implement the first small Firebase/web scaffolding milestone, with a professional responsive attendance dashboard architecture. Clearly distinguish implemented work from planned work, and give me the exact commands and checks to verify it.
