import {initializeApp} from 'firebase-admin/app';
import {getFirestore, FieldValue} from 'firebase-admin/firestore';
import {onRequest} from 'firebase-functions/v2/https';
import {parseScan, validToken, digest, payloadHash} from './validation';

initializeApp();
const db = getFirestore();
class Rejection extends Error {
  constructor(public status: number, message: string) { super(message); }
}

// Region is provisional; confirm database location before deployment.
export const ingestAttendance = onRequest({region:'asia-southeast1', maxInstances:3, cors:false}, async (req, res) => {
  res.set('Cache-Control', 'no-store');
  if (req.method !== 'POST') { res.set('Allow','POST').status(405).json({error:'POST required'}); return; }
  if (!req.is('application/json') || Buffer.byteLength(JSON.stringify(req.body) ?? '') > 2048) {
    res.status(400).json({error:'Expected small JSON body'}); return;
  }
  let scan;
  try { scan = parseScan(req.body); } catch { res.status(400).json({error:'Invalid payload'}); return; }
  const token = /^Bearer ([^\s]+)$/.exec(req.get('Authorization') ?? '')?.[1] ?? '';
  const eventKey = digest(`${scan.deviceId}:${scan.eventId}`);
  try {
    const duplicate = await db.runTransaction(async tx => {
      const deviceRef = db.doc(`devices/${scan.deviceId}`);
      const device = (await tx.get(deviceRef)).data();
      if (!device || device.status !== 'active' || !validToken(token, device.tokenHash)) throw new Rejection(401, 'Unauthorized device');
      if (typeof device.branchId !== 'string' || !/^[A-Za-z0-9_-]{1,80}$/.test(device.branchId)) throw new Rejection(403, 'Device branch unavailable');
      const branch = (await tx.get(db.doc(`branches/${device.branchId}`))).data();
      if (branch?.isActive !== true) throw new Rejection(403, 'Inactive branch');
      const eventRef = db.doc(`attendanceEvents/${eventKey}`);
      const previous = await tx.get(eventRef);
      if (previous.exists) {
        if (previous.get('payloadHash') !== payloadHash(scan)) throw new Rejection(409, 'Event ID already used');
        return true;
      }
      const matches = await tx.get(db.collection('employees')
        .where('assignedDeviceId', '==', scan.deviceId).where('rfidUid', '==', scan.rfidUid).limit(2));
      const employee = matches.docs[0]?.data();
      if (matches.size !== 1 || employee?.status !== 'active' || employee.branchId !== device.branchId ||
          employee.fingerprintTemplateId !== scan.fingerprintTemplateId) throw new Rejection(403, 'Employee mapping rejected');
      tx.create(eventRef, {
        ...scan, employeeId: matches.docs[0].id, branchId: device.branchId,
        payloadHash: payloadHash(scan), receivedAt: FieldValue.serverTimestamp(),
        result:'verified', verificationMethod:'rfid_and_fingerprint'
      });
      tx.update(deviceRef, {lastSeenAt:FieldValue.serverTimestamp()});
      tx.set(db.doc(`deviceStatuses/${scan.deviceId}`), {
        name: typeof device.name === 'string' ? device.name : scan.deviceId,
        branchId: device.branchId, status: device.status,
        lastSeenAt: FieldValue.serverTimestamp()
      }, {merge: true});
      return false;
    });
    res.status(duplicate ? 200 : 201).json({eventId:scan.eventId, receiptId:eventKey, duplicate});
  } catch (error) {
    if (error instanceof Rejection) { res.status(error.status).json({error:error.message}); return; }
    // Do not log body, biometric references, or bearer credentials.
    console.error('Attendance transaction failed');
    res.status(503).json({error:'Service unavailable; retry with the same event ID'});
  }
});
