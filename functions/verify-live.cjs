const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const {management,projectRef}=require('./supabase-management.cjs');
async function main(){
 const header=fs.readFileSync(path.resolve(__dirname,'../firmware/esp32/secrets.h'),'utf8');
 const token=/DEVICE_TOKEN = "([A-Za-z0-9_-]{32,256})"/.exec(header)?.[1];
 if(!token)throw Error('Missing local device token');
 const url=`https://${projectRef}.supabase.co/functions/v1/ingest-attendance`;
 const scan={version:1,deviceId:'device-1',eventId:'negative-connection-check',rfidUid:'A3 F0 70 33',fingerprintTemplateId:2};
 const before=await management('database/query','POST',{query:'select count(*)::integer as total from public.attendance_events'});
 const request=(body,credential)=>fetch(url,{method:'POST',headers:{'Content-Type':'application/json',Authorization:`Bearer ${credential}`},body:JSON.stringify(body)});
 assert.equal((await request(scan,'x'.repeat(64))).status,401);
 assert.equal((await request({...scan,branchId:'spoofed'},token)).status,400);
 const wrong=await request(scan,token);
 if(wrong.status!==403)throw Error(`Valid credential + wrong fingerprint must be rejected; got ${wrong.status}: ${await wrong.text()}`);
 const after=await management('database/query','POST',{query:'select count(*)::integer as total from public.attendance_events'});
 assert.equal(after[0].total,before[0].total);
 console.log('LIVE INGESTION VERIFIED: wrong credential 401, forged field 400, wrong fingerprint 403; no synthetic attendance inserted.');
 const config=fs.readFileSync(path.resolve(__dirname,'../web/.env.local'),'utf8');
 const get=k=>new RegExp(`^${k}=(.+)$`,'m').exec(config)?.[1].trim();
 const credentials=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../.local/administrator.json'),'utf8'));
 const login=await fetch(`https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=${get('VITE_FIREBASE_API_KEY')}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...credentials,returnSecureToken:true})});
 const session=await login.json();if(!login.ok)throw Error('Firebase live verification login failed');
 const {createClient}=require('../web/node_modules/@supabase/supabase-js');
 const client=createClient(get('VITE_SUPABASE_URL'),get('VITE_SUPABASE_PUBLISHABLE_KEY'),{accessToken:async()=>session.idToken});
 const channel=client.channel('live-connection-verification').on('postgres_changes',{event:'INSERT',schema:'public',table:'attendance_events'},()=>{});
 try{
  await new Promise((resolve,reject)=>{
   const timer=setTimeout(()=>reject(Error('Realtime connection timed out')),20000);
   channel.subscribe(status=>{if(status==='SUBSCRIBED'){clearTimeout(timer);resolve()}if(status==='CHANNEL_ERROR'||status==='TIMED_OUT'){clearTimeout(timer);reject(Error(`Realtime ${status}`))}});
  });
  console.log('LIVE REALTIME VERIFIED: Firebase-authenticated Supabase attendance subscription accepted.');
 }finally{await client.removeChannel(channel);await client.realtime.disconnect()}
}
main().catch(e=>{console.error(e.message);process.exitCode=1});
