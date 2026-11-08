// Reuse only the Supabase CLI credential authorized for this project.
// Credentials stay in memory and are never included in logs or saved files.
const {Entry,findCredentials}=require('@napi-rs/keyring');
const projectRef='yrqfqvrgbsssuigxkbrc';
function credential(){
 let value=null;
 try{value=new Entry('Supabase CLI','supabase').getPassword()}catch{}
 if(!value){const matches=findCredentials('Supabase CLI','Supabase CLI:supabase');value=matches.find(c=>c.account==='supabase')?.password}
 if(!value)throw Error('No authorized Supabase CLI credential. Run Supabase login first.');
 if(!/^[\x20-\x7e]+$/.test(value)){
  const bytes=[];for(let i=0;i<value.length;i++){const code=value.charCodeAt(i);bytes.push(code&255);if(code>>8)bytes.push(code>>8)}
  value=Buffer.from(bytes).toString('utf8');
 }
 if(value.startsWith('go-keyring-base64:'))value=Buffer.from(value.slice('go-keyring-base64:'.length),'base64').toString('utf8');
 if(!/^sb[p_]/.test(value))throw Error('Invalid Supabase CLI credential format');
 return value;
}
async function management(suffix,method='GET',body){
 const response=await fetch(`https://api.supabase.com/v1/projects/${projectRef}/${suffix}`,{method,headers:{Authorization:`Bearer ${credential()}`,'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});
 const data=await response.json();if(!response.ok)throw Error(`Supabase ${response.status}: ${data.message??data.error??'Request failed'}`);return data;
}
module.exports={management,projectRef};
if(require.main===module){
 (async()=>{
  const existing=await management('config/auth/third-party-auth');
  if(!Array.isArray(existing))throw Error('Unexpected auth integration response');
  const issuer='https://securetoken.google.com/camp-attendance-d0d37';
  const configured=existing.some(p=>p.oidc_issuer_url===issuer);
  if(process.argv[2]==='enable-firebase'&&!configured){
   await management('config/auth/third-party-auth','POST',{oidc_issuer_url:issuer,jwks_url:'https://www.googleapis.com/service_accounts/v1/jwk/securetoken@system.gserviceaccount.com'});
   console.log('Firebase third-party auth integration created.');
  }else console.log(JSON.stringify({firebaseIntegration:configured,integrationCount:existing.length}));
 })().catch(e=>{console.error(e.message);process.exitCode=1});
}
