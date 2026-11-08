// Uses the already-authorized Firebase CLI account in memory; never prints tokens.
const path = require('node:path');
const fs = require('node:fs');
const cliAuth = require(path.join(process.env.APPDATA,'npm/node_modules/firebase-tools/lib/auth.js'));
const account = cliAuth.getAllAccounts().find(a=>a.user.email==='attendance474@gmail.com');
const project = 'camp-attendance-d0d37';
async function main(){
  if(!account)throw Error('Sign into Firebase CLI with the project account first.');
  const credential=await cliAuth.getAccessToken(account.tokens.refresh_token,['https://www.googleapis.com/auth/cloud-platform','https://www.googleapis.com/auth/firebase']);
  const api=async(url,method='GET',body)=>{
    const r=await fetch(url,{method,headers:{Authorization:`Bearer ${credential.access_token}`,'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});
    const data=await r.json();if(!r.ok)throw Error(`${r.status}: ${data.error?.message??'API request failed'}`);return data;
  };
  const mode=process.argv[2]??'inspect';
  if(mode==='inspect'){
    const billing=await api(`https://cloudbilling.googleapis.com/v1/projects/${project}/billingInfo`);
    console.log(JSON.stringify({projectId:project,billingEnabled:billing.billingEnabled===true}));
  } else if(mode==='inspect-auth') {
    const config=await api(`https://identitytoolkit.googleapis.com/admin/v2/projects/${project}/config`);
    console.log(JSON.stringify({emailSignIn:config.signIn?.email,authorizedDomains:config.authorizedDomains,subtype:config.subtype}));
  } else if(mode==='configure-auth') {
    // The project owner must initialize basic Firebase Auth in Console first.
    await api(`https://identitytoolkit.googleapis.com/admin/v2/projects/${project}/config`);
    const config=await api(`https://identitytoolkit.googleapis.com/admin/v2/projects/${project}/config?updateMask=signIn.email`, 'PATCH', {signIn:{email:{enabled:true,passwordRequired:true}}});
    console.log(JSON.stringify({emailSignIn:config.signIn?.email,configured:true}));
  } else if(mode==='enable-firestore') {
    const op=await api(`https://serviceusage.googleapis.com/v1/projects/${project}/services/firestore.googleapis.com:enable`,'POST',{});
    console.log(JSON.stringify({operation:op.name,done:op.done===true}));
  } else if(mode==='web-config') {
    const config=await api(`https://firebase.googleapis.com/v1beta1/projects/${project}/webApps/1:933823149791:web:b4142bb91c19704ba55b6a/config`);
    const text=`VITE_FIREBASE_API_KEY=${config.apiKey}\nVITE_FIREBASE_AUTH_DOMAIN=${config.authDomain}\nVITE_FIREBASE_PROJECT_ID=${config.projectId}\nVITE_FIREBASE_APP_ID=${config.appId}\n`;
    fs.writeFileSync(path.resolve(__dirname,'../web/.env.local'),text);
    console.log('Firebase public Web App configuration saved to web/.env.local.');
  } else throw Error('Unknown setup mode');
}
main().catch(e=>{console.error(e.message);process.exitCode=1});
