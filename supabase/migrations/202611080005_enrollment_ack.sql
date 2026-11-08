-- Track waiting for Mega separately from the confirmed RFID prompt.
create or replace function public.device_enrollment(p_device_id text,p_token_hash text,p_job_id uuid default null,
 p_step text default null,p_uid text default null,p_finger integer default null,p_error text default null)
returns jsonb language plpgsql security definer set search_path = '' as $$
declare terminal public.devices; job public.enrollment_jobs; employee public.employees;
begin
 select * into terminal from public.devices where id=p_device_id for update;
 if terminal.id is null or terminal.status<>'active' or not exists (
  select 1 from private.device_credentials where device_id=p_device_id and token_hash=p_token_hash
 ) then return jsonb_build_object('status',401,'error','Unauthorized device'); end if;
 if not exists(select 1 from public.branches where id=terminal.branch_id and is_active)
  then return jsonb_build_object('status',403,'error','Inactive branch'); end if;
 update public.enrollment_jobs set status='expired',step='expired',error_code='TIMEOUT'
  where device_id=p_device_id and status in ('queued','running') and expires_at < now();
 if p_job_id is null then
  select * into job from public.enrollment_jobs where device_id=p_device_id and status in ('queued','running');
  if not found then return jsonb_build_object('status',200,'job',null); end if;
  update public.enrollment_jobs set status='running' where id=job.id;
  return jsonb_build_object('status',200,'job',job.id);
 end if;
 select * into job from public.enrollment_jobs where id=p_job_id and device_id=p_device_id for update;
 if not found then return jsonb_build_object('status',404,'error','Unknown job'); end if;
 if job.status in ('completed','failed','expired') then
  if job.status='completed' and p_step='completed' and (p_uid is distinct from job.rfid_uid or p_finger is distinct from job.fingerprint_template_id)
   then return jsonb_build_object('status',409,'error','Result conflict'); end if;
  return jsonb_build_object('status',200,'jobId',job.id,'state',job.status,'ack',true);
 end if;
 if p_step='failed' then
  update public.enrollment_jobs set status='failed',step='failed',error_code=left(coalesce(p_error,'HARDWARE_ERROR'),64) where id=job.id;
 elsif p_step='completed' then
  if p_uid is null or p_uid !~ '^([A-F0-9]{2} ){3}[A-F0-9]{2}(( [A-F0-9]{2}){3}|( [A-F0-9]{2}){6})?$' or p_finger is null or p_finger not between 2 and 65535
   then return jsonb_build_object('status',400,'error','Invalid hardware result'); end if;
  select * into employee from public.employees where id=job.employee_id for update;
  if employee.assigned_device_id<>p_device_id or employee.branch_id<>terminal.branch_id or employee.status<>'inactive' or employee.rfid_uid is not null or employee.fingerprint_template_id is not null
    or exists(select 1 from public.employees where assigned_device_id=p_device_id and (rfid_uid=p_uid or fingerprint_template_id=p_finger)) then
   update public.enrollment_jobs set status='failed',step='failed',error_code='MAPPING_CONFLICT' where id=job.id;
   return jsonb_build_object('status',200,'jobId',job.id,'state','failed','ack',true);
  end if;
  update public.employees set rfid_uid=p_uid,fingerprint_template_id=p_finger,status='active' where id=employee.id;
  update public.enrollment_jobs set status='completed',step='completed',rfid_uid=p_uid,fingerprint_template_id=p_finger,completed_at=now() where id=job.id;
 elsif p_step in ('waiting_for_mega','tap_card','place_finger','remove_finger','place_again','saving') then
  update public.enrollment_jobs set step=p_step,status='running' where id=job.id;
  return jsonb_build_object('status',200,'jobId',job.id,'ack',false);
 else return jsonb_build_object('status',400,'error','Invalid step'); end if;
 select * into job from public.enrollment_jobs where id=p_job_id;
 return jsonb_build_object('status',200,'jobId',job.id,'state',job.status,'ack',true);
end; $$;
revoke all on function public.device_enrollment(text,text,uuid,text,text,integer,text) from public,anon,authenticated;
grant execute on function public.device_enrollment(text,text,uuid,text,text,integer,text) to service_role;
