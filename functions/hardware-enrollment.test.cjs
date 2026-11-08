const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const {PGlite}=require('@electric-sql/pglite');
test('Hardware enrollment: authorization, terminal isolation, activation, replay, conflicts and timeout',async()=>{
 const db=new PGlite();
 try{
  await db.exec(`create role anon; create role authenticated; create role service_role;
   create schema auth; create function auth.jwt() returns jsonb language sql stable as $$select coalesce(nullif(current_setting('request.jwt.claims',true),''),'{}')::jsonb$$;
   grant usage on schema auth to authenticated; create publication supabase_realtime;`);
  for(const name of ['202611080001_attendance.sql','202611080002_employee_registration.sql','202611080003_hardware_enrollment.sql','202611080004_cancel_enrollment.sql','202611080005_enrollment_ack.sql'])await db.exec(fs.readFileSync(`${__dirname}/../supabase/migrations/${name}`,'utf8'));
  await db.exec(`insert into public.branches(id,name) values('a','A'),('b','B');
   insert into public.devices(id,name,branch_id,status) values('one','One','a','active'),('two','Two','b','active');
   insert into private.device_credentials values('one',repeat('a',64)),('two',repeat('b',64));
   insert into public.user_profiles values('manager','team_leader',array['a'],true);
   insert into public.employees(id,employee_code,full_name,branch_id,assigned_device_id,status,employment_type,rfid_uid,fingerprint_template_id)
    values('original','ORIG','Original employee','a','one','active','full_time','A3 F0 70 33',1),
    ('new','NEW','New employee','a','one','inactive','full_time',null,null),
    ('other','OTHER','Another employee','a','one','inactive','full_time',null,null),
    ('foreign','FOREIGN','Foreign employee','b','two','inactive','full_time',null,null);`);
  async function manager(operation){
   await db.query(`select set_config('request.jwt.claims',$1,false)`,[JSON.stringify({sub:'manager',iss:'https://securetoken.google.com/camp-attendance-d0d37',aud:'camp-attendance-d0d37'})]);
   await db.exec('set role authenticated');try{return await operation()}finally{await db.exec('reset role')}
  }
  const start=id=>manager(()=>db.query('select * from public.start_employee_enrollment($1)',[id]));
  const cancel=id=>manager(()=>db.query('select * from public.cancel_employee_enrollment($1)',[id]));
  const device=async(job=null,step=null,uid=null,finger=null,error=null,id='one',hash='a'.repeat(64))=>(await db.query('select public.device_enrollment($1,$2,$3,$4,$5,$6,$7) as result',[id,hash,job,step,uid,finger,error])).rows[0].result;
  await assert.rejects(start('foreign'),/denied/);
  const job=(await start('new')).rows[0];assert.equal(job.status,'queued');
  assert.equal((await start('new')).rows[0].id,job.id);
  await assert.rejects(start('other'),/another employee/);
  assert.equal((await device(null,null,null,null,null,'one','x'.repeat(64))).status,401);
  assert.equal((await device(job.id,'completed','AA BB CC DD',2,null,'two','b'.repeat(64))).status,404);
  assert.equal((await device()).job,job.id);
  assert.equal((await db.query('select step from public.enrollment_jobs where id=$1',[job.id])).rows[0].step,'waiting_for_device');
  assert.equal((await device(job.id,'waiting_for_mega')).ack,false);
  assert.equal((await db.query('select step from public.enrollment_jobs where id=$1',[job.id])).rows[0].step,'waiting_for_mega');
  await device(job.id,'tap_card');
  assert.equal((await db.query('select step from public.enrollment_jobs where id=$1',[job.id])).rows[0].step,'tap_card');
  await manager(async()=>{assert.equal((await db.query('select * from public.enrollment_jobs')).rows.length,1);await assert.rejects(db.query('select public.device_enrollment($1,$2)', ['one','a'.repeat(64)]));});
  assert.equal((await device(job.id,'place_again')).ack,false);
  assert.equal((await db.query("select status from public.employees where id='new'")).rows[0].status,'inactive');
  assert.equal((await device(job.id,'completed','AA BB CC DD',2)).state,'completed');
  assert.equal((await device(job.id,'completed','AA BB CC DD',2)).ack,true);
  assert.equal((await device(job.id,'completed','AA BB CC DE',3)).status,409);
  const enrolled=(await db.query("select * from public.employees where id='new'")).rows[0];assert.equal(enrolled.status,'active');assert.equal(enrolled.fingerprint_template_id,2);
  assert.equal((await db.query("select fingerprint_template_id from public.employees where id='original'")).rows[0].fingerprint_template_id,1);
  const scan=await db.query("select public.ingest_device_event('one',$1,'newscan','AA BB CC DD',2) as result",['a'.repeat(64)]);assert.equal(scan.rows[0].result.status,201);
  const conflict=(await start('other')).rows[0];assert.equal((await device(conflict.id,'completed','AA BB CC DD',3)).state,'failed');
  assert.equal((await db.query("select status from public.employees where id='other'")).rows[0].status,'inactive');
  const timed=(await start('other')).rows[0];await db.query("update public.enrollment_jobs set expires_at=now()-interval '1 minute' where id=$1",[timed.id]);
  assert.equal((await device(timed.id,'completed','11 22 33 44',3)).state,'expired');
  const failed=(await start('other')).rows[0];assert.equal((await device(failed.id,'failed',null,null,'CARD_TIMEOUT')).state,'failed');
  const cancelled=(await start('other')).rows[0];
  const foreignJob=(await db.query("insert into public.enrollment_jobs(employee_id,device_id,branch_id,requested_by) values('foreign','two','b','other-manager') returning id")).rows[0];
  await assert.rejects(cancel(foreignJob.id),/denied/);
  assert.equal((await cancel(cancelled.id)).rows[0].error_code,'CANCELLED');
  assert.equal((await cancel(cancelled.id)).rows[0].error_code,'CANCELLED');
  assert.equal((await device(cancelled.id,'completed','11 22 33 44',3)).state,'failed');
  assert.equal((await db.query("select status from public.employees where id='other'")).rows[0].status,'inactive');
  const replacement=(await start('other')).rows[0];assert.notEqual(replacement.id,cancelled.id);
  assert.equal((await cancel(job.id)).rows[0].status,'completed');
  assert.equal((await device(replacement.id,'failed',null,null,'MEGA_NOT_RESPONDING')).state,'failed');
  assert.equal((await db.query('select error_code from public.enrollment_jobs where id=$1',[replacement.id])).rows[0].error_code,'MEGA_NOT_RESPONDING');
  await db.exec("update public.user_profiles set is_active=false");await assert.rejects(start('other'),/denied/);
  await assert.rejects(cancel(cancelled.id),/denied/);
 }finally{await db.close()}
});
