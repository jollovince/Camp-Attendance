# Firebase setup and ingestion contract

## Before deployment

Open project `camp-attendance-d0d37` in Firebase Console. Confirm the project ID, billing plan, existing resources, and selected Firestore region. Create a production-mode Firestore database only if none exists. Confirm the function region before deploying. Enable Email/Password Auth later for the dashboard; no public admin signup is provided. Firebase Console state and deployed resources have not been verified here.

Cloud Functions deployment requires the appropriate Firebase billing plan. Consult https://firebase.google.com/docs/functions/get-started and https://firebase.google.com/docs/functions/local-emulator for current prerequisites. No service-account private key belongs in firmware or the repository.

## Server-managed provisioning

Use trusted Admin SDK tooling or the local Emulator UI for test data. Never expose public write rules for setup. No production seed script is provided to avoid accidental writes.

Create a fictional test branch and employee, and a terminal mapping:

- `branches/branch-1`: `isActive: true`, `name: "Test Branch"`.
- `devices/device-1`: `status: "active"`, `branchId: "branch-1"`, `tokenHash: <SHA-256 hex of a random device bearer token>`.
- `employees/employee-1`: `status: "active"`, `branchId: "branch-1"`, `assignedDeviceId: "device-1"`, `rfidUid: "A3 F0 70 33"`, `fingerprintTemplateId: 1`, `fullName: "Sample Employee"`.

Generate at least 32 random bytes for each device token using a trusted local tool. Store only its SHA-256 hash in Firestore and keep the token in ignored local device configuration. Never send a token in chat. Rotate the hash or set device status inactive to revoke. Token possession authenticates a terminal; it does not independently prove a physical fingerprint scan occurred. Firmware and terminal access remain trusted.

## HTTP contract

POST JSON with `Authorization: Bearer <device-token>` over verified HTTPS:

```json
{
  "version": 1,
  "deviceId": "device-1",
  "eventId": "unique-boot-id-000001",
  "rfidUid": "A3 F0 70 33",
  "fingerprintTemplateId": 1
}
```

Generate the event ID once at capture and preserve it across retries. Use a durable queue before promising offline support. The server resolves the employee and branch; the device cannot supply employee ID, branch, timestamp, or attendance action. The Mega UART payload requires an ESP32 adapter, not direct forwarding.

Responses: 201 new receipt; 200 identical retry; 400 malformed request; 401 invalid/inactive device; 403 inactive branch or mismatched employee mapping; 409 changed payload under an existing event ID; 503 transient failure (retry the same event ID with backoff).

The transaction creates one immutable `attendanceEvents/{sha256(deviceId:eventId)}` record with authoritative `receivedAt` and updates terminal `lastSeenAt`. Duplicate receipt lookup occurs after device and branch validation, so revoked devices cannot retrieve receipts. It deliberately does not toggle time-in/time-out. A fresh ID with repeated data is a distinct event; business duplicate/cooldown policy remains to be defined. A successful retry does not update lastSeenAt; this field describes the latest newly accepted event, not heartbeat health.

## Required integration checks

Run against emulators before any deployment: successful mapping; identical and concurrent retries result in one record; conflicting event ID returns 409; wrong fingerprint/card/branch and revoked device fail; unauthenticated Firestore reads/writes fail; no failed request creates attendance. Repeat with hardware after ESP32 transport is implemented. These checks have not yet been run.

## Dashboard architecture (next milestone)

Use React/TypeScript with Firebase Auth and Firestore listeners after role and branch rules are implemented. Keep a separate, clearly labeled local demo mode with fictional data. Begin with login, branch filter, live verified scans and employee views; add schedules, DTR and reporting only after attendance action policy is agreed. Use Asia/Manila when rendering timestamps, while retaining Firestore Timestamp storage.
