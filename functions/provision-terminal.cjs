const fs=require('node:fs');
const path=require('node:path');
const {createHash}=require('node:crypto');
const {management,projectRef}=require('./supabase-management.cjs');
async function main(){
 const header=fs.readFileSync(path.resolve(__dirname,'../firmware/esp32/secrets.h'),'utf8');
 const token=/DEVICE_TOKEN = "([A-Za-z0-9_-]{32,256})"/.exec(header)?.[1];
 if(!token)throw Error('No local device token configured');
 const hash=createHash('sha256').update(token).digest('hex');
 const sql=`begin;
 insert into public.branches(id,name) values('branch-1','Prototype test branch') on conflict(id) do nothing;
 insert into public.devices(id,name,branch_id,status,firmware_version) values('device-1','Prototype ESP32 terminal','branch-1','active','bridge-1') on conflict(id) do nothing;
 insert into private.device_credentials(device_id,token_hash) values('device-1','${hash}') on conflict(device_id) do nothing;
 insert into public.employees(id,employee_code,full_name,branch_id,assigned_device_id,status,employment_type,rfid_uid,fingerprint_template_id)
 values('prototype-employee-1','TEST-0001','Prototype Test Employee','branch-1','device-1','active','full_time','A3 F0 70 33',1) on conflict(id) do nothing;
 commit;`;
 await management('database/query','POST',{query:sql});
 const rows=await management('database/query','POST',{query:`select d.id,d.branch_id,d.status,c.token_hash='${hash}' as credential_matches from public.devices d join private.device_credentials c on c.device_id=d.id where d.id='device-1'`});
 if(rows.length!==1||!rows[0].credential_matches)throw Error('Existing terminal credential differs. Refusing to overwrite.');
 const url=`https://${projectRef}.supabase.co/functions/v1/ingest-attendance`;
 // Confirm the deployed URL exists, but do not inject a fabricated attendance scan.
 const probe=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({version:1,deviceId:'device-1',eventId:'connection-check',rfidUid:'A3 F0 70 33',fingerprintTemplateId:1})});
 if(probe.status!==401)throw Error(`Unexpected unauthenticated endpoint status ${probe.status}`);
 const updated=header.replace(/const char\* INGEST_URL = "[^"]*";/,`const char* INGEST_URL = "${url}";`);
 fs.writeFileSync(path.resolve(__dirname,'../firmware/esp32/secrets.h'),updated);
 console.log(JSON.stringify({deviceId:'device-1',branchId:'branch-1',testMappingRegistered:true,unauthenticatedRequestRejected:true,endpointConfigured:true}));
}
main().catch(e=>{console.error(e.message);process.exitCode=1});
