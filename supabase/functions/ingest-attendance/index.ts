import {createClient} from 'npm:@supabase/supabase-js@2';
import {parseScan} from '../_shared/validation.ts';

Deno.serve(async (request: Request) => {
  const reply = (body: unknown, status: number) => Response.json(body, {status, headers:{'Cache-Control':'no-store'}});
  if(request.method !== 'POST') return new Response('POST required',{status:405,headers:{Allow:'POST'}});
  if(!request.headers.get('content-type')?.startsWith('application/json')) return reply({error:'Expected JSON'},400);
  // Bound the actual stream, not just the caller-controlled Content-Length.
  const reader=request.body?.getReader();
  if(!reader)return reply({error:'Missing payload'},400);
  const chunks:Uint8Array[]=[];let size=0;
  try {
    while(true){const chunk=await reader.read();if(chunk.done)break;size+=chunk.value.length;if(size>2048){await reader.cancel();return reply({error:'Payload too large'},413)}chunks.push(chunk.value)}
  }catch{return reply({error:'Invalid payload'},400)}
  const bytes=new Uint8Array(size);let offset=0;for(const chunk of chunks){bytes.set(chunk,offset);offset+=chunk.length}
  let scan;
  try{scan=parseScan(JSON.parse(new TextDecoder().decode(bytes)))}catch{return reply({error:'Invalid payload'},400)}
  const token=/^Bearer ([A-Za-z0-9_-]{32,256})$/.exec(request.headers.get('authorization')??'')?.[1];
  if(!token)return reply({error:'Unauthorized device'},401);
  const tokenHash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(token))),b=>b.toString(16).padStart(2,'0')).join('');
  const url=Deno.env.get('SUPABASE_URL'),key=Deno.env.get('SUPABASE_SERVICE_ROLE_KEY');
  if(!url||!key)return reply({error:'Service unavailable'},503);
  const client=createClient(url,key,{auth:{persistSession:false,autoRefreshToken:false}});
  const {data,error}=await client.rpc('ingest_device_event',{p_device_id:scan.deviceId,p_token_hash:tokenHash,p_event_id:scan.eventId,p_rfid_uid:scan.rfidUid,p_fingerprint_id:scan.fingerprintTemplateId});
  if(error||!data)return reply({error:'Service unavailable; retry the same event ID'},503);
  const {status,...receipt}=data;
  return reply(receipt,status);
});
