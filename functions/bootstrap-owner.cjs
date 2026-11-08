const fs=require('node:fs');
const path=require('node:path');
const {randomBytes}=require('node:crypto');
const {initializeApp}=require('firebase-admin/app');
const {getAuth}=require('firebase-admin/auth');
const cliAuth=require(path.join(process.env.APPDATA,'npm/node_modules/firebase-tools/lib/auth.js'));
const {management,projectRef}=require('./supabase-management.cjs');
const account=cliAuth.getAllAccounts().find(a=>a.user.email==='attendance474@gmail.com');
async function main(){
 if(!account)throw Error('Project owner Firebase CLI login is missing');
 const app=initializeApp({projectId:'camp-attendance-d0d37',credential:{getAccessToken:()=>cliAuth.getAccessToken(account.tokens.refresh_token,['https://www.googleapis.com/auth/cloud-platform','https://www.googleapis.com/auth/firebase'])}});
 const auth=getAuth(app),email=account.user.email;
 let user,created=false;
 const credentialsPath=path.resolve(__dirname,'../.local/administrator.json');
 try{user=await auth.getUserByEmail(email)}catch(e){
  if(e.code!=='auth/user-not-found')throw e;
  const password=randomBytes(24).toString('base64url');
  // Persist before account creation so a failed process cannot lose the password.
  fs.mkdirSync(path.dirname(credentialsPath),{recursive:true});
  fs.writeFileSync(credentialsPath,JSON.stringify({email,password},null,2),{mode:0o600});
  user=await auth.createUser({email,password,displayName:'Camp of Coffee Owner'});created=true;
 }
 await auth.setCustomUserClaims(user.uid,{...user.customClaims,role:'authenticated'});
 if(!/^[A-Za-z0-9_-]+$/.test(user.uid))throw Error('Unexpected Firebase UID format');
 await management('database/query','POST',{query:`insert into public.user_profiles(firebase_uid,role,branch_ids,is_active) values('${user.uid}','owner','{}',true) on conflict(firebase_uid) do nothing`});
 console.log(JSON.stringify({firebaseUid:user.uid,ownerEmail:email,accountCreated:created,credentialsFile:created?'.local/administrator.json':'Existing account password preserved',supabaseProfileProvisioned:true}));
 const credentials=fs.existsSync(credentialsPath)?JSON.parse(fs.readFileSync(credentialsPath,'utf8')):null;
 if(!credentials||credentials.email!==email){console.log('Use the existing account password to verify live login.');return}
 const config=fs.readFileSync(path.resolve(__dirname,'../web/.env.local'),'utf8');
 const get=k=>new RegExp(`^${k}=(.+)$`,'m').exec(config)?.[1].trim();
 const signed=await fetch(`https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=${get('VITE_FIREBASE_API_KEY')}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...credentials,returnSecureToken:true})});
 const session=await signed.json();if(!signed.ok)throw Error(`Firebase sign-in failed: ${session.error?.message??signed.status}`);
 const result=await fetch(`https://${projectRef}.supabase.co/rest/v1/user_profiles?firebase_uid=eq.${user.uid}&select=firebase_uid,role,is_active`,{headers:{apikey:get('VITE_SUPABASE_PUBLISHABLE_KEY'),Authorization:`Bearer ${session.idToken}`}});
 const profiles=await result.json();if(!result.ok||profiles.length!==1||profiles[0].role!=='owner')throw Error(`Supabase Firebase-token authorization failed (${result.status})`);
 console.log('LIVE LOGIN VERIFIED: Firebase password login -> Supabase owner profile read. No tokens logged.');
}
main().catch(e=>{console.error(e.message);process.exitCode=1});
