// Read-only diagnostic. Never prints credentials or changes enrollment jobs.
const fs = require('node:fs');
const path = require('node:path');
async function main() {
 const root = path.resolve(__dirname, '..');
 const config = fs.readFileSync(path.join(root, 'web/.env.local'), 'utf8');
 const get = key => new RegExp(`^${key}=(.+)$`, 'm').exec(config)?.[1].trim();
 const credentials = JSON.parse(fs.readFileSync(path.join(root, '.local/administrator.json'), 'utf8'));
 const login = await fetch(`https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=${get('VITE_FIREBASE_API_KEY')}`, {
  method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({...credentials, returnSecureToken: true})
 });
 const session = await login.json();
 if (!login.ok) throw Error('Administrator sign-in failed');
 const response = await fetch(`${get('VITE_SUPABASE_URL')}/rest/v1/enrollment_jobs?select=id,device_id,status,step,error_code,created_at,expires_at&order=created_at.desc&limit=5`, {
  headers: {apikey: get('VITE_SUPABASE_PUBLISHABLE_KEY'), Authorization: `Bearer ${session.idToken}`}
 });
 if (!response.ok) throw Error(`Enrollment read failed: HTTP ${response.status}`);
 console.log(JSON.stringify(await response.json(), null, 2));
}
main().catch(e => {console.error(e.message); process.exitCode = 1;});
