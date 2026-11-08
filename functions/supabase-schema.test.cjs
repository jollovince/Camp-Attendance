const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {PGlite}=require('@electric-sql/pglite');

test('Supabase SQL: device receipt, mapping, idempotency, RLS and credential isolation',async()=>{
 const db=new PGlite();
 try{
  await db.exec(`create role anon; create role authenticated; create role service_role;
   create schema auth;
   create function auth.jwt() returns jsonb language sql stable as $$select coalesce(nullif(current_setting('request.jwt.claims',true),''),'{}')::jsonb$$;
   grant usage on schema auth to authenticated;
   create publication supabase_realtime;`);
  await db.exec(fs.readFileSync(path.join(__dirname,'../supabase/migrations/202611080001_attendance.sql'),'utf8'));
  await db.exec(`insert into public.branches(id,name) values('a','Test A'),('b','Test B');
   insert into public.devices(id,name,branch_id,status) values('device-1','Test terminal','a','active');
   insert into private.device_credentials values('device-1',repeat('a',64));
   insert into public.employees(id,employee_code,full_name,branch_id,assigned_device_id,status,employment_type,rfid_uid,fingerprint_template_id)
   values('test-1','TEST-1','Fictional employee','a','device-1','active','full_time','A3 F0 70 33',1);
   insert into public.user_profiles values('leader','team_leader',array['a'],true),('owner','owner','{}',true);`);
  const ingest=async(event='event-1',finger=1,hash='a'.repeat(64))=>(await db.query(`select public.ingest_device_event('device-1',$1,$2,'A3 F0 70 33',$3) as receipt`,[hash,event,finger])).rows[0].receipt;
  assert.equal((await ingest('event-1',1,'b'.repeat(64))).status,401);
  assert.equal((await ingest('event-1',2)).status,403);
  assert.equal((await ingest()).status,201);
  assert.equal((await ingest()).status,200);
  assert.equal((await ingest('event-1',2)).status,409);
  assert.equal((await db.query('select * from public.attendance_events')).rows.length,1);
  await db.exec(`update public.devices set status='inactive' where id='device-1'`);
  assert.equal((await ingest()).status,401);
  async function asUser(sub,operation,issuer='https://securetoken.google.com/camp-attendance-d0d37'){
   await db.query(`select set_config('request.jwt.claims',$1,false)`,[JSON.stringify({sub,iss:issuer,aud:'camp-attendance-d0d37',role:'authenticated'})]);
   await db.exec('set role authenticated');
   try{return await operation()}finally{await db.exec('reset role')}
  }
  assert.deepEqual((await asUser('leader',()=>db.query('select id from public.branches order by id'))).rows,[{id:'a'}]);
  assert.equal((await asUser('owner',()=>db.query('select id from public.branches'))).rows.length,2);
  assert.equal((await asUser('leader',()=>db.query('select * from public.branches'),'https://securetoken.google.com/untrusted-project')).rows.length,0);
  await assert.rejects(asUser('owner',()=>db.query('select * from private.device_credentials')));
  await assert.rejects(asUser('owner',()=>db.exec(`update public.attendance_events set rfid_uid='forged'`)));
  await assert.rejects(asUser('owner',()=>ingest('forged')));
  await db.exec(`update public.user_profiles set is_active=false where firebase_uid='leader'`);
  assert.equal((await asUser('leader',()=>db.query('select * from public.attendance_events'))).rows.length,0);
 }finally{await db.close()}
});

test('Supabase request parser rejects spoofed fields and invalid UIDs',async()=>{
 const {parseScan}=await import('../supabase/functions/_shared/validation.ts');
 const valid={version:1,deviceId:'device-1',eventId:'event-1',rfidUid:'A3 F0 70 33',fingerprintTemplateId:1};
 assert.deepEqual(parseScan(valid),valid);
 for(const extra of [{branchId:'spoofed'},{rfidUid:'A3 F0 70'},{fingerprintTemplateId:0},{deviceId:'../x'},{eventId:''}])assert.throws(()=>parseScan({...valid,...extra}));
});
