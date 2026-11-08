import {createHash, timingSafeEqual} from 'node:crypto';

export interface Scan {
  version: 1; deviceId: string; eventId: string; rfidUid: string;
  fingerprintTemplateId: number;
}
export function parseScan(value: unknown): Scan {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error('Invalid payload');
  const v = value as Record<string, unknown>;
  const id = /^[A-Za-z0-9_-]{1,80}$/;
  const fields = ['version', 'deviceId', 'eventId', 'rfidUid', 'fingerprintTemplateId'];
  if (Object.keys(v).length !== fields.length || fields.some(k => !(k in v)) ||
      v.version !== 1 || typeof v.deviceId !== 'string' || !id.test(v.deviceId) ||
      typeof v.eventId !== 'string' || !id.test(v.eventId) ||
      typeof v.rfidUid !== 'string' || !/^(?:[A-F0-9]{2} ){3}(?:[A-F0-9]{2})(?: [A-F0-9]{2}){0,6}$/.test(v.rfidUid) ||
      ![4, 7, 10].includes(v.rfidUid.split(' ').length) ||
      !Number.isInteger(v.fingerprintTemplateId) || Number(v.fingerprintTemplateId) < 1 || Number(v.fingerprintTemplateId) > 65535) {
    throw new Error('Invalid payload');
  }
  return v as unknown as Scan;
}
export function digest(value: string): string {
  return createHash('sha256').update(value).digest('hex');
}
export function validToken(token: string, storedHash: unknown): boolean {
  if (token.length < 32 || token.length > 256 || typeof storedHash !== 'string' || !/^[a-f0-9]{64}$/.test(storedHash)) return false;
  return timingSafeEqual(Buffer.from(digest(token), 'hex'), Buffer.from(storedHash, 'hex'));
}
export function payloadHash(scan: Scan): string {
  return digest(JSON.stringify([scan.version, scan.deviceId, scan.eventId, scan.rfidUid, scan.fingerprintTemplateId]));
}
