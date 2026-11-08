import {useEffect, useState, type FormEvent} from 'react';
import {supabase} from './supabase';

export function EmployeeRegistration({onSaved}:{onSaved:()=>void}) {
 const [open,setOpen]=useState(false),[busy,setBusy]=useState(false),[error,setError]=useState(''),[success,setSuccess]=useState('');
 const [terminals,setTerminals]=useState<{id:string;name:string;branch_id:string}[]>([]);
 useEffect(()=>{if(!open||!supabase)return;let active=true;void supabase.from('devices').select('id,name,branch_id').eq('status','active').then(({data,error})=>{if(active){setTerminals(data??[]);if(error)setError('Unable to load permitted terminals. Try again.')}});return()=>{active=false}},[open]);
 async function save(event:FormEvent<HTMLFormElement>){
  event.preventDefault();if(!supabase||busy)return;
  const form=event.currentTarget,data=new FormData(form);setBusy(true);setError('');setSuccess('');
  try{
   const {error}=await supabase.rpc('register_employee',{p_full_name:String(data.get('fullName')).trim(),p_age:Number(data.get('age')),p_position:String(data.get('position')).trim(),p_device_id:String(data.get('device')),p_employment_type:String(data.get('employmentType'))});
   if(error)throw error;
   form.reset();setOpen(false);setSuccess('Employee registered. Select Enroll RFID & fingerprint in the directory to complete hardware enrollment.');onSaved();
  }catch(e){setError(e instanceof Error?e.message:(e as {message?:string}).message??'Registration failed. Please try again.')}finally{setBusy(false)}
 }
 return <section className="panel registration"><div className="panelhead"><div><h2>Register an employee</h2><p>Fill in the employee details, then enroll their RFID card and fingerprint on the terminal.</p></div><button className="primary" disabled={busy} onClick={()=>{setOpen(!open);setError('');setSuccess('')}}>{open?'Close form':'Add employee'}</button></div>
 {success&&<p className="registration-success" role="status">{success}</p>}
 {open&&<form className="employee-form" onSubmit={save}>
 <label>Full name<input name="fullName" required minLength={2} maxLength={120} autoComplete="name"/></label>
 <label>Age<input name="age" required type="number" min="1" max="120" step="1"/></label>
 <label>Position<input name="position" required maxLength={100} placeholder="e.g. Barista, Cashier, Manager"/></label>
 <label>Assigned terminal / branch<select name="device" required defaultValue=""><option value="" disabled>Select terminal</option>{terminals.map(t=><option key={t.id} value={t.id}>{t.name} / {t.branch_id}</option>)}</select></label>
 <label>Employment type<select name="employmentType"><option value="full_time">Full time</option><option value="part_time">Part time</option></select></label>
 <p className="enrollment-note">This saves the employee profile. Fingerprints must be captured using the physical sensor; use Enroll RFID & fingerprint in the employee directory after saving.</p>
 {error&&<div className="error" role="alert">{error}</div>}
 <button className="primary" disabled={busy||!terminals.length}>{busy?'Saving employee…':'Save employee'}</button>
 </form>}
 </section>;
}
