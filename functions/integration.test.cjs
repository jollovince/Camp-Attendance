// Run only with the local Firebase Emulator Suite, never against live resources.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {randomBytes, createHash} = require('node:crypto');
const {initializeApp} = require('firebase-admin/app');
const {getFirestore} = require('firebase-admin/firestore');
process.env.FIRESTORE_EMULATOR_HOST = '127.0.0.1:8080';
process.env.FIREBASE_AUTH_EMULATOR_HOST = '127.0.0.1:9099';
const projectId = 'demo-camp-attendance';
const db = getFirestore(initializeApp({projectId}));
const endpoint = `http://127.0.0.1:5001/${projectId}/asia-southeast1/ingestAttendance`;
const root = `http://127.0.0.1:8080/v1/projects/${projectId}/databases/(default)/documents`;
const token = randomBytes(32).toString('hex');
const body = {version:1, deviceId:'integration-device', eventId:randomBytes(16).toString('hex'), rfidUid:'A3 F0 70 33', fingerprintTemplateId:1};
const post = (data=body, credential=token) => fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/json',Authorization:`Bearer ${credential}`},body:JSON.stringify(data)});
async function account(role, branchIds) {
  const result = await fetch('http://127.0.0.1:9099/identitytoolkit.googleapis.com/v1/accounts:signUp?key=emulator-only', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({email:`${randomBytes(8).toString('hex')}@example.test`,password:randomBytes(16).toString('hex'),returnSecureToken:true})});
  assert.equal(result.status,200);
  const user = await result.json();
  await db.doc(`users/${user.localId}`).set({role,branchIds,isActive:true});
  return user.idToken;
}
const read = (path, idToken) => fetch(`${root}/${path}`,{headers:idToken?{Authorization:`Bearer ${idToken}`}:{}});

test('emulator ingestion: device authentication, employee mapping, atomic retries and immutable receipt', async()=>{
  await db.doc('branches/integration-branch').set({isActive:true});
  await db.doc('devices/integration-device').set({status:'active',branchId:'integration-branch',tokenHash:createHash('sha256').update(token).digest('hex')});
  await db.doc('employees/integration-employee').set({fullName:'Fictional integration employee',status:'active',branchId:'integration-branch',assignedDeviceId:body.deviceId,rfidUid:body.rfidUid,fingerprintTemplateId:1});
  assert.equal((await post(body,'wrong')).status,401);
  assert.equal((await post({...body,branchId:'forged'})).status,400);
  assert.equal((await post({...body,fingerprintTemplateId:2})).status,403);
  const results=await Promise.all([post(),post(),post()]);
  assert.deepEqual(results.map(r=>r.status).sort(),[200,200,201]);
  assert.equal((await post({...body,fingerprintTemplateId:2})).status,409);
  const stored=await db.collection('attendanceEvents').where('deviceId','==',body.deviceId).get();
  assert.equal(stored.size,1);
  assert.equal(stored.docs[0].get('employeeId'),'integration-employee');
  assert.ok(stored.docs[0].get('receivedAt').toDate());
  const publicDevice=(await db.doc('deviceStatuses/integration-device').get()).data();
  assert.equal(publicDevice.branchId,'integration-branch');
  assert.equal('tokenHash' in publicDevice,false);
  await db.doc('devices/integration-device').update({status:'inactive'});
  assert.equal((await post()).status,401);
});

test('emulator rules: owner, scoped leader, private device credentials and denied client writes',async()=>{
  await db.doc('employees/rules-a').set({branchId:'rules-branch-a',fullName:'Fictional A'});
  await db.doc('employees/rules-b').set({branchId:'rules-branch-b',fullName:'Fictional B'});
  const owner=await account('owner',[]),leader=await account('team_leader',['rules-branch-a']);
  assert.equal((await read('employees/rules-a')).status,403);
  assert.equal((await read('employees/rules-a',leader)).status,200);
  assert.equal((await read('employees/rules-b',leader)).status,403);
  assert.equal((await read('employees/rules-b',owner)).status,200);
  assert.equal((await read('devices/integration-device',owner)).status,403);
  const write=await fetch(`${root}/attendanceEvents/forged`,{method:'PATCH',headers:{'Content-Type':'application/json',Authorization:`Bearer ${owner}`},body:JSON.stringify({fields:{branchId:{stringValue:'rules-branch-a'}}})});
  assert.equal(write.status,403);
});
