# Camp of Coffee attendance

## Offline localhost with Laragon

A separate offline admin is available at `http://127.0.0.1:8787/`. It uses Laragon MySQL (`camp_attendance_offline`), a local hashed administrator login, and direct Mega USB for registration/enrollment and attendance. Firebase, Supabase and ESP32 Wi-Fi are not required in this mode. Start MySQL in Laragon, build with `npm.cmd --prefix web run build`, then run `powershell -ExecutionPolicy Bypass -File local/start.ps1`. Upload the updated Mega sketch and close Arduino Serial Monitor before selecting the Mega USB port in the admin. See [offline setup](local/README.md). Local records are separate from cloud data and are not automatically synchronized.

**Current architecture: Firebase Authentication + Hosting, Supabase Postgres + Realtime + Edge Function.** The user selected both providers on November 8, 2026. See [current hybrid setup and verified status](docs/hybrid-setup.md). The Firestore/Cloud Functions notes below describe the earlier prototype and are retained for reference; they are not the selected live data path.

Live website: **https://camp-attendance-d0d37.web.app**. Firebase project: `camp-attendance-d0d37`. Supabase project: `yrqfqvrgbsssuigxkbrc` (Seoul). The attendance schema, Edge Function, Firebase third-party Auth integration, test terminal mapping, and owner profile are configured. ESP32 firmware was uploaded to COM25. A real RFID/fingerprint scan reached Supabase on November 8, 2026 at 14:16:37 Asia/Manila and was verified readable by the Firebase-authenticated administrator. Replaying that same event returned the existing receipt, with one canonical record.

## Website

```powershell
npm.cmd --prefix web ci
npm.cmd --prefix web run dev
```

Open the Vite URL (normally http://127.0.0.1:5173/) or the live Hosting URL above. The website has overview, live scans, employee directory, device views, branch/date/search filters, and CSV export. Both providers are configured. Live mode reads Supabase records; optional demo mode uses fictional data.

Both public client configs are saved in ignored `web/.env.local`. Administrator login is `attendance474@gmail.com`; its generated app password is in ignored `.local/administrator.json` (not the Google account password). This account carries the required Firebase `authenticated` claim and has a Supabase owner profile. The client cannot grant itself permissions.

## Local verification on Windows

```powershell
cd C:\Users\DELL\OneDrive\Desktop\Camp_Attendace
npm.cmd --prefix functions ci
npm.cmd --prefix functions test
node --test functions/supabase-schema.test.cjs
npx.cmd --yes deno check supabase/functions/ingest-attendance/index.ts
```

The active API uses Supabase's Deno runtime. Node 22 is only the earlier Firebase function scaffold's target; local tests used Node 24. Use `npm.cmd` because PowerShell blocks `npm.ps1` on this machine.

For integration testing, install Firebase CLI and the Java runtime required by the current Firestore emulator, then:

```powershell
npx.cmd firebase-tools emulators:start --project demo-camp-attendance --only auth,firestore,functions
```

Use the demo project for isolated local work. The emulator URL is printed by the CLI; no live API URL is assumed. See [setup and contract](docs/firebase-setup.md) before provisioning records or testing requests.

## Current boundaries

- A 16x2 I2C LCD was added to the Mega source and compiled successfully. It is not uploaded or physically verified yet. See [LCD setup](docs/lcd-setup.md) for Mega wiring and the upload file.

- microSD backup is deferred by user request and disabled in source (`CAMP_SD_BACKUP_ENABLED=0`). The user will upload the updated ESP32 sketch; until then, an older SD-required firmware may still pause cloud uploads. The 20-event NVS queue remains enabled. See [microSD notes](docs/microsd-backup.md).

- TypeScript build passed; all four unit tests passed on the workstation's Node 24. Tests cover payload validation, credential matching, and stable payload hashes. Node 22 deployment-runtime verification remains pending.
- Earlier Firebase emulator integration tests passed. Supabase SQL/RLS and request validation tests passed; Edge Function Deno type check passed.
- Live checks passed: Firebase password sign-in, Supabase owner profile access using the Firebase JWT, authenticated realtime subscription, and rejection of invalid device credentials, forged fields, and wrong fingerprint mapping. These checks inserted no synthetic attendance.
- ESP32 has a 20-event persistent queue and verified HTTPS upload. One successful physical scan reached the server and was readable through the administrator data path. Physical rejection scenarios, power-loss recovery and offline reconciliation remain to be tested; browser-rendered appearance was not verified.
- Schedules, DTR, employee editing, and payroll are not implemented. No time-in/time-out categorization is inferred.
- Supabase clients have branch-scoped read access only. Credential hashes are in a private schema and the ingestion RPC is callable only by the trusted Edge Function service role. The original Mega verification logic and wiring are unchanged.

The source handoff is retained in [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md). Firmware notes are in [firmware/README.md](firmware/README.md).
# Employee registration

In **Employees → Add employee**, an active owner, administrator, or team leader can register a full name, age, position, employment type, and permitted active terminal. The server assigns the terminal's branch and generates the employee code. Registrations are audited with the manager's Firebase UID.

New profiles remain inactive and display **Pending enrollment**. Select **Enroll RFID & fingerprint** beside the employee, then follow the live card and two-scan fingerprint prompts. Both updated Mega and ESP32 sketches must be uploaded first. Existing prototype credentials are preserved. See [upload, enrollment and recovery instructions](docs/employee-enrollment.md). Physical enrollment validation is pending the user uploads and sensor test.

Schema: `supabase/migrations/202611080002_employee_registration.sql`. Registration permission checks: `node --test functions/employee-registration.test.cjs`.

