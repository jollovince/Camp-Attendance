# Firebase + Supabase hardware connection

Updated November 8, 2026. The user selected both providers.

## Responsibilities

- Firebase Authentication: administrator login, with `role: authenticated` custom claim for Supabase.
- Firebase Hosting: static React dashboard.
- Supabase Postgres + RLS: branch-scoped employee and attendance data.
- Supabase Realtime: attendance updates in the dashboard.
- Supabase Edge Function: authenticated ESP32 ingestion.
- Mega: existing RFID-first fingerprint verification, unchanged.
- ESP32: validated UART reception, 20-event durable NVS queue, certificate-verified HTTPS, retained event IDs on retry.

Firebase Cloud Functions and Firestore are retained as the earlier prototype backend in `functions/`, but are no longer the selected live ingestion/storage path. Do not deploy them for this hybrid setup. Blaze billing is not enabled.

## What has actually been verified

- USB detection: Mega COM23; ESP32 COM25, ESP32-D0WD-V3 revision 3.1, 4 MB flash.
- ESP32 bridge compiled against installed `esp32:esp32` 3.3.12.
- Full pre-upload flash backup: ignored `firmware/backups/esp32-COM25-before-bridge.bin` (4,194,304 bytes). Contains private device state; keep local.
- Four Firebase API unit tests passed in the earlier milestone.
- Two local Firebase emulator integration tests passed, covering receipt deduplication and read/write rules. These validate the earlier backend, not the Supabase deployment.
- Supabase schema and request validation tested locally with PGlite: device authentication, same-employee mapping, idempotency, conflicting retries, revoked device, branch access, wrong Firebase issuer, denied client writes and hidden credential table.
- Hybrid React production build and Supabase Edge Function Deno type check passed. The local website returned HTTP 200 after the change.
- Firebase CLI account `attendance474@gmail.com` successfully authorized. Web App registered; public config saved to ignored `web/.env.local`.
- Project billing is disabled. Firestore API enablement was started before the hybrid architecture was selected; no Firestore database or attendance records were created by that step.

## Live deployment status

Supabase project `yrqfqvrgbsssuigxkbrc` is healthy in `ap-northeast-2` (Seoul). Its public/private schemas and Edge Function list were empty before setup. The migration was applied, `ingest-attendance` deployed, and the hosted Firebase third-party Auth integration registered. The prototype branch, terminal and card/template mapping are provisioned. No synthetic attendance record was inserted.

Live checks passed: Firebase Email/Password sign-in; Supabase owner-profile read with the Firebase ID token; authenticated realtime subscription; API responses 401 for invalid credentials, 400 for forged fields, 403 for wrong fingerprint mapping. Device token hashes remain in the private schema.

Firmware was uploaded to COM25 with successful hash verification and a confirmed bridge boot message. The user subsequently reported a successful physical card-plus-fingerprint verification and UART send. Supabase contains the corresponding real event `device-1:0901c7068828734b80c0c24f1f7e0f6d`, received November 8, 2026 at 06:16:37.888903 UTC (14:16:37 Asia/Manila). The Firebase-authenticated administrator can read it through the same REST data path used by the dashboard. Replaying the identical event returned HTTP 200 with `duplicate: true`; a changed fingerprint under the same event ID returned 409. One canonical record remained. No synthetic successful scan was inserted.

Website deployment succeeded at https://camp-attendance-d0d37.web.app (Hosting version `1e8de2155ede20c5`). Website HTML and original logo both returned HTTP 200. Browser visual/click QA remains unavailable. Generated administrator app credentials are in ignored `.local/administrator.json`; this is separate from the Google account password.

The following checklist is retained as the setup procedure. Setup, firmware upload, one successful physical scan through the server, and authenticated dashboard-data access are verified. Browser-rendered display, physical rejection scenarios, offline recovery, and interrupted-power behavior remain unverified.

## Remote setup procedure

Target Supabase project supplied by the user: `yrqfqvrgbsssuigxkbrc`. Supabase CLI was authenticated as `attendance474@gmail.com` using the user's browser verification. Firebase basic Authentication was initialized by the user in Console, then Email/Password was enabled through the project config API. Billing remains disabled; paid Identity Platform was not enabled. Server management credentials stay in memory and use the authorized CLI stores.

Once connected:

1. Inspect existing resources; choose an available free project/region deliberately.
2. Apply `supabase/migrations/202611080001_attendance.sql` using trusted migration tools. It creates fresh tables; review conflicts before applying to an existing database.
3. In Supabase Authentication → Third-party Auth, register Firebase project `camp-attendance-d0d37`. Local TOML alone does not configure hosted third-party Auth.
4. Use trusted Firebase Admin tooling to set each approved user's custom claim `role: authenticated`. App owner/admin permissions are held separately in `public.user_profiles`, keyed by Firebase UID; never use a browser-editable role.
5. Deploy `supabase/functions/ingest-attendance`. Its JWT gateway is disabled only because it uses per-device bearer authentication, checked by the server-only transaction RPC. Keep the Supabase service-role key exclusively in the Edge Function environment.
6. Provision the fictional prototype branch, terminal, and employee matching the actual test card/template: device `device-1`, RFID `A3 F0 70 33`, template 1. Store SHA-256 of the locally generated `DEVICE_TOKEN` in `private.device_credentials`. The raw token is already in ignored `firmware/esp32/secrets.h`; do not rotate it casually or print it into logs.
7. Set the actual deployed Edge Function HTTPS URL in `INGEST_URL` in the ignored header. Configured URL: `https://yrqfqvrgbsssuigxkbrc.supabase.co/functions/v1/ingest-attendance`.
8. Set `VITE_SUPABASE_URL` and `VITE_SUPABASE_PUBLISHABLE_KEY` in ignored `web/.env.local`, restart Vite, and rebuild for Hosting.
9. Upload compiled bridge to COM25 only after configuration is complete. The original Mega sketch remains untouched. Check serial at 115200 and perform a real RFID/fingerprint scan, then verify one server receipt and one dashboard row. Wrong credentials/card/template and a retried event must be tested against the deployed service too.

Firebase Auth integration reference: https://supabase.com/docs/guides/auth/third-party/firebase-auth

## Local commands

```powershell
npm.cmd --prefix web run dev
npm.cmd --prefix web run build
node --test functions/supabase-schema.test.cjs
npx.cmd --yes deno check supabase/functions/ingest-attendance/index.ts
```

The SQL test runs an embedded Postgres instance with mock JWT claims. It does not prove hosted Firebase token verification, realtime connectivity, or Edge gateway configuration. Real Supabase deployment and physical end-to-end testing are still required.

## Hardware behavior and limits

The ESP32 accepts only the existing newline-terminated Mega payload. It validates UID length/format and template ID, ignores the claimed employee ID for server mapping, assigns a random event ID once, and atomically saves the bounded queue with ESP32 Preferences before reporting `QUEUED`.

HTTPS uses the installed ESP32 CA bundle and NTP time. After HTTP 200/201 with a matching event receipt, the event is removed from NVS. Failed requests retain their IDs and retry with backoff. A full queue rejects additional events with an explicit serial error. The unchanged Mega does not wait for queue/server acknowledgements, so transport-level loss before ESP32 persistence is still possible. The serial RX buffer reduces blocking-upload overflow; this is not a complete acknowledged Mega-to-ESP32 protocol.

No upload, sensor-reset, re-enrollment, or actual attendance scan is implied by successful compilation. No time-in/time-out, shifts, or payroll rules are inferred.
