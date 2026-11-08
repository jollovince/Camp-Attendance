import {test} from 'node:test';
import assert from 'node:assert/strict';
import {parseScan, digest, validToken, payloadHash} from './validation';
const scan = {version:1, deviceId:'device-1', eventId:'boot-123-1', rfidUid:'A3 F0 70 33', fingerprintTemplateId:1};
test('accepts the versioned hardware mapping', () => assert.deepEqual(parseScan(scan), scan));
test('rejects forged mapping fields, traversal, invalid UIDs and template IDs', () => {
  for (const change of [{branchId:'other'}, {deviceId:'../x'}, {rfidUid:'A3 F0'}, {fingerprintTemplateId:0}, {version:2}, {eventId:''}]) {
    assert.throws(() => parseScan({...scan, ...change}));
  }
});
test('requires a strong matching token hash', () => {
  const token = 'a'.repeat(64);
  assert.equal(validToken(token, digest(token)), true);
  assert.equal(validToken('b'.repeat(64), digest(token)), false);
  assert.equal(validToken('', digest('')), false);
  assert.equal(validToken(token, 'invalid'), false);
});
test('retry has identical hash; changed verification does not', () => {
  const parsed = parseScan(scan);
  assert.equal(payloadHash(parsed), payloadHash({...parsed}));
  assert.notEqual(payloadHash(parsed), payloadHash({...parsed, fingerprintTemplateId:2}));
});
