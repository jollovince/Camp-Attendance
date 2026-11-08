# Website milestone

**Superseded implementation notes below:** Current live website is https://camp-attendance-d0d37.web.app. It uses Firebase Authentication and Supabase Postgres/Realtime, and includes the supplied Camp of Coffee logo. Both provider configs and the owner profile are provisioned; live JWT authorization/realtime checks passed. See [current deployment status](hybrid-setup.md). The original Firestore/access-blocker notes below are historical.

The React + TypeScript + Vite dashboard lives in `web/`. Coffee greens, warm ivory, restrained typography, responsive tables, focus states, and labeled controls provide a consistent provisional brand. No official assets were supplied.

Available locally: overview; verified scans; branch/date/search filters; employee directory; sanitized terminal registration and last accepted event; CSV export; administrator Email/Password login; Firestore snapshot subscriptions; explicit demo, loading, empty, and failure states. Demo records are fictional and dated to the current local preview day. Live timestamps display in Asia/Manila. The live scan query loads at most the latest 200 received events; historical reporting and pagination are not yet implemented.

## Firebase access blocker

Firebase CLI is installed and has logged-in accounts, but the available accounts cannot list Web Apps in `camp-attendance-d0d37`. The first lookup returned HTTP 403 `PERMISSION_DENIED`. No app registration, remote data writes, permission changes, or deployments were attempted after this failure.

Sign in with the account that owns or has project access:

```powershell
firebase.cmd login:add
firebase.cmd apps:list WEB --project camp-attendance-d0d37 --account YOUR_PROJECT_ACCOUNT
```

Then retrieve the existing Web App configuration using `firebase.cmd apps:sdkconfig WEB APP_ID --project camp-attendance-d0d37 --account YOUR_PROJECT_ACCOUNT`. If there is no registered Web App, register one in Project settings or with the Firebase CLI. Store the public configuration in the variables listed in `web/.env.example`; do not provide service-account files or passwords.

## Deployment preparation

`firebase.json` includes Firebase Hosting for `web/dist` with an SPA fallback and a build predeploy hook. Before a deployment, verify the database region, existing production security rules, active user profiles, and required indexes. Test the new rules with emulators before publishing them. Client writes are denied; active owner/admin/team-leader profiles receive appropriate branch read access. `devices` is never client-readable because it holds credential hashes. `deviceStatuses` is a sanitized projection updated by newly accepted ingestion events. Existing devices need a trusted projection migration to appear before their next accepted event.

No public registration or privilege editing is exposed. Do not grant an owner role to an untrusted account. Ingestion still uses the server-side device mappings and does not infer time-in/time-out.

Verification limits: local HTTP success and production compilation are checked separately from live Firebase access. Browser automation reported no available browsers, so visual rendering, click flows, mobile behavior, and keyboard navigation remain unverified. Rules and transaction integration require emulator tests; unit tests do not substitute for those checks.
