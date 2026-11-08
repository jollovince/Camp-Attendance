import {createClient} from 'npm:@supabase/supabase-js@2';
Deno.serve(async(request:Request)=>{
 const reply=(body:unknown,status:number)=>Response.json(body,{status,headers:{'Cache-Control':'no-store'}});
 if(request.method!=='POST')return reply({error:'POST required'},405);
 const token=/^Bearer ([A-Za-z0-9_-]{32,256})$/.exec(request.headers.get('authorization')??'')?.[1];
 if(!token)return reply({error:'Unauthorized device'},401);
 if(!request.headers.get('content-type')?.startsWith('application/json'))return reply({error:'Expected JSON'},400);
 let body:Record<string,unknown>;
 try{
  const reader=request.body?.getReader();if(!reader)throw Error();let text='',length=0;const decoder=new TextDecoder();
  while(true){const chunk=await reader.read();if(chunk.done)break;length+=chunk.value.length;if(length>1024){await reader.cancel();return reply({error:'Payload too large'},413)}text+=decoder.decode(chunk.value,{stream:true})}text+=decoder.decode();
  body=JSON.parse(text);
  if(!body||typeof body!=='object'||Array.isArray(body)||Object.keys(body).some(k=>!['deviceId','jobId','step','uid','finger','error'].includes(k))||typeof body.deviceId!=='string'||!/^[A-Za-z0-9_-]{1,80}$/.test(body.deviceId))throw Error();
  if(body.jobId!==undefined&&(typeof body.jobId!=='string'||!/^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$/.test(body.jobId)))throw Error();
  for(const field of ['step','uid','error'])if(body[field]!==undefined&&(typeof body[field]!=='string'||String(body[field]).length>64))throw Error();
  if(body.finger!==undefined&&(!Number.isInteger(body.finger)||Number(body.finger)<2||Number(body.finger)>65535))throw Error();
 }catch{return reply({error:'Invalid request'},400)}
 const hash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(token))),b=>b.toString(16).padStart(2,'0')).join('');
 const url=Deno.env.get('SUPABASE_URL'),key=Deno.env.get('SUPABASE_SERVICE_ROLE_KEY');if(!url||!key)return reply({error:'Unavailable'},503);
 const client=createClient(url,key,{auth:{persistSession:false,autoRefreshToken:false}});
 const {data,error}=await client.rpc('device_enrollment',{p_device_id:body.deviceId,p_token_hash:hash,p_job_id:body.jobId??null,p_step:body.step??null,p_uid:body.uid??null,p_finger:body.finger??null,p_error:body.error??null});
 if(error||!data)return reply({error:'Unavailable; retry same job'},503);
 const {status,...result}=data;return reply(result,status);
});
