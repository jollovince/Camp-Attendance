// Read access and negative probes only: never creates jobs or biometric results.
const fs=require('node:fs');const path=require('node:path');const assert=require('node:assert/strict');
async function main(){
 const header=fs.readFileSync(path.resolve(__dirname,'../firmware/esp32/secrets.h'),'utf8');
 const token=/DEVICE_TOKEN = "([A-Za-z0-9_-]{32,256})"/.exec(header)?.[1];if(!token)throw Error('Missing local terminal credential');
 const config=fs.readFileSync(path.resolve(__dirname,'../web/.env.local'),'utf8');const get=k=>new RegExp(`^${k}=(.+)$`,'m').exec(config)?.[1].trim();
 const endpoint=`${get('VITE_SUPABASE_URL')}/functions/v1/device-enrollment`;
 const request=(body,credential)=>fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/json',Authorization:`Bearer ${credential}`},body:JSON.stringify(body)});
 assert.equal((await request({deviceId:'device-1'},'x'.repeat(64))).status,401);
 assert.equal((await request({deviceId:'device-1',employeeId:'spoofed'},token)).status,400);
 const credentials=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../.local/administrator.json'),'utf8'));
 const login=await fetch(`https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=${get('VITE_FIREBASE_API_KEY')}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...credentials,returnSecureToken:true})});
 const session=await login.json();if(!login.ok)throw Error('Administrator sign-in failed');
 const response=await fetch(`${get('VITE_SUPABASE_URL')}/rest/v1/enrollment_jobs?select=id,status&limit=1`,{headers:{apikey:get('VITE_SUPABASE_PUBLISHABLE_KEY'),Authorization:`Bearer ${session.idToken}`}});
 assert.equal(response.status,200);assert.ok(Array.isArray(await response.json()));
 console.log('LIVE ENROLLMENT VERIFIED: invalid device 401, forged field 400, Firebase administrator read 200. No enrollment jobs or hardware results created.');
}
main().catch(e=>{console.error(e.message);process.exitCode=1});
