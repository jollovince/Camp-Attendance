export interface Scan {version:1;deviceId:string;eventId:string;rfidUid:string;fingerprintTemplateId:number}
export function parseScan(value:unknown):Scan{
  if(!value||typeof value!=='object'||Array.isArray(value))throw Error('Invalid payload');
  const v=value as Record<string,unknown>,keys=['version','deviceId','eventId','rfidUid','fingerprintTemplateId'];
  const id=/^[A-Za-z0-9_-]{1,80}$/;
  if(Object.keys(v).length!==keys.length||keys.some(k=>!(k in v))||v.version!==1||typeof v.deviceId!=='string'||!id.test(v.deviceId)||typeof v.eventId!=='string'||!id.test(v.eventId)||typeof v.rfidUid!=='string'||!/^[A-F0-9]{2}( [A-F0-9]{2})+$/.test(v.rfidUid)||![4,7,10].includes(v.rfidUid.split(' ').length)||!Number.isInteger(v.fingerprintTemplateId)||Number(v.fingerprintTemplateId)<1||Number(v.fingerprintTemplateId)>65535)throw Error('Invalid payload');
  return v as unknown as Scan;
}
