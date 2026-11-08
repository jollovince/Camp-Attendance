const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const {PGlite}=require('@electric-sql/pglite');
test('Registration enforces manager branch access and keeps incomplete hardware enrollment inactive',async()=>{
 const db=new PGlite();
 try{
  await db.exec(`create role anon; create role authenticated; create role service_role;
   create schema auth; create function auth.jwt() returns jsonb language sql stable as $$select coalesce(nullif(current_setting('request.jwt.claims',true),''),'{}')::jsonb$$;
   grant usage on schema auth to authenticated; create publication supabase_realtime;`);
  for(const name of ['202611080001_attendance.sql','202611080002_employee_registration.sql'])await db.exec(fs.readFileSync(`${__dirname}/../supabase/migrations/${name}`,'utf8'));
  await db.exec(`insert into public.branches(id,name) values('a','A'),('b','B');
   insert into public.devices(id,name,branch_id,status) values('one','One','a','active'),('two','Two','b','active');
   insert into public.user_profiles values('manager','team_leader',array['a'],true);`);
  async function register(device='one',age=25,issuer='https://securetoken.google.com/camp-attendance-d0d37'){
   await db.query(`select set_config('request.jwt.claims',$1,false)`,[JSON.stringify({sub:'manager',iss:issuer,aud:'camp-attendance-d0d37'})]);
   await db.exec('set role authenticated');
   try{return await db.query(`select * from public.register_employee('Test employee',$1,'Barista',$2,'full_time')`,[age,device])}finally{await db.exec('reset role')}
  }
  const result=await register();
  assert.equal(result.rows[0].age,25);assert.equal(result.rows[0].position,'Barista');
  assert.equal(result.rows[0].status,'inactive');assert.equal(result.rows[0].rfid_uid,null);
  assert.equal((await db.query('select * from private.employee_registration_audit')).rows.length,1);
  await assert.rejects(register('two'),/permission/);
  await assert.rejects(register('one',0),/Check/);
  await assert.rejects(register('one',25,'https://securetoken.google.com/wrong'),/permission/);
  await assert.rejects(db.exec(`update public.employees set status='active'`),/active_employee_credentials/);
  await db.exec(`update public.user_profiles set is_active=false`);
  await assert.rejects(register(),/permission/);
 }finally{await db.close()}
});

