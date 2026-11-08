// Replays an existing real scan only; never fabricates a successful scan.
const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const {management,projectRef}=require('./supabase-management.cjs');
async function main(){
 const rows=await management('database/query','POST',{query:"select * from public.attendance_events where device_id='device-1' order by received_at desc limit 1"});
 if(rows.length!==1)throw Error('No real hardware scan is stored yet.');
 const event=rows[0];
 const countQuery=`select count(*)::integer as total from public.attendance_events where id='${event.id.replaceAll("'","''")}'`;
 const header=fs.readFileSync(path.resolve(__dirname,'../firmware/esp32/secrets.h'),'utf8');
 const token=/DEVICE_TOKEN = "([A-Za-z0-9_-]{32,256})"/.exec(header)?.[1];
 if(!token)throw Error('Missing local terminal credential');
 const body={version:1,deviceId:event.device_id,eventId:event.event_id,rfidUid:event.rfid_uid,fingerprintTemplateId:event.fingerprint_template_id};
 const post=value=>fetch(`https://${projectRef}.supabase.co/functions/v1/ingest-attendance`,{method:'POST',headers:{'Content-Type':'application/json',Authorization:`Bearer ${token}`},body:JSON.stringify(value)});
 const retry=await post(body);
 assert.equal(retry.status,200);assert.equal((await retry.json()).duplicate,true);
 assert.equal((await post({...body,fingerprintTemplateId:body.fingerprintTemplateId+1})).status,409);
 assert.equal((await management('database/query','POST',{query:countQuery}))[0].total,1);
 const config=fs.readFileSync(path.resolve(__dirname,'../web/.env.local'),'utf8');
 const get=k=>new RegExp(`^${k}=(.+)$`,'m').exec(config)?.[1].trim();
 const credentials=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../.local/administrator.json'),'utf8'));
 const login=await fetch(`https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=${get('VITE_FIREBASE_API_KEY')}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...credentials,returnSecureToken:true})});
 const session=await login.json();if(!login.ok)throw Error('Administrator sign-in failed');
 const search=new URLSearchParams({id:`eq.${event.id}`,select:'id,received_at,employee_id,device_id'});
 const result=await fetch(`${get('VITE_SUPABASE_URL')}/rest/v1/attendance_events?${search}`,{headers:{apikey:get('VITE_SUPABASE_PUBLISHABLE_KEY'),Authorization:`Bearer ${session.idToken}`}});
 assert.equal(result.status,200);assert.equal((await result.json()).length,1);
 console.log(JSON.stringify({hardwareEvent:event.id,receivedAt:event.received_at,employeeId:event.employee_id,administratorCanRead:true,identicalRetryStatus:200,changedRetryStatus:409,canonicalRecordCount:1}));
}
main().catch(e=>{console.error(e.message);process.exitCode=1});
