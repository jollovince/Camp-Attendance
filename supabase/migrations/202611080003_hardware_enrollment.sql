create table public.enrollment_jobs (
 id uuid primary key default gen_random_uuid(), employee_id text not null references public.employees(id),
 device_id text not null references public.devices(id), branch_id text not null references public.branches(id),
 requested_by text not null, created_at timestamptz not null default now(),
 expires_at timestamptz not null default now() + interval '15 minutes',
 status text not null default 'queued' check (status in ('queued','running','completed','failed','expired')),
 step text not null default 'waiting_for_device', error_code text,
 rfid_uid text, fingerprint_template_id integer, completed_at timestamptz
);
create unique index one_terminal_enrollment on public.enrollment_jobs(device_id) where status in ('queued','running');
alter table public.enrollment_jobs enable row level security;
revoke all on public.enrollment_jobs from anon, authenticated;
grant select on public.enrollment_jobs to authenticated;
create policy enrollment_read on public.enrollment_jobs for select to authenticated using (private.can_read_branch(branch_id));

create function public.start_employee_enrollment(p_employee_id text) returns public.enrollment_jobs
language plpgsql security definer set search_path = '' as $$
declare employee public.employees; terminal public.devices; job public.enrollment_jobs;
begin
 select * into employee from public.employees where id=p_employee_id;
 if not private.can_read_branch(employee.branch_id) then raise exception 'Employee access denied.' using errcode='42501'; end if;
 select * into terminal from public.devices where id=employee.assigned_device_id for update;
 -- Lock terminal before the employee, as the device completion endpoint does.
 select * into employee from public.employees where id=p_employee_id for update;
 if employee.status <> 'inactive' or employee.rfid_uid is not null or employee.fingerprint_template_id is not null then
  raise exception 'This employee already has hardware credentials.' using errcode='22023'; end if;
 if terminal.status <> 'active' or terminal.branch_id <> employee.branch_id or not exists (
  select 1 from public.branches where id=terminal.branch_id and is_active
 ) then raise exception 'Terminal or branch is inactive.' using errcode='22023'; end if;
 update public.enrollment_jobs set status='expired',step='expired',error_code='TIMEOUT'
  where device_id=terminal.id and status in ('queued','running') and expires_at < now();
 select * into job from public.enrollment_jobs where device_id=terminal.id and status in ('queued','running');
 if found then
  if job.employee_id=p_employee_id then return job; end if;
  raise exception 'This terminal is enrolling another employee. Finish it or wait for its timeout.' using errcode='22023';
 end if;
 insert into public.enrollment_jobs(employee_id,device_id,branch_id,requested_by)
  values(employee.id,terminal.id,employee.branch_id,auth.jwt()->>'sub') returning * into job;
 return job;
end; $$;
revoke all on function public.start_employee_enrollment(text) from public, anon;
grant execute on function public.start_employee_enrollment(text) to authenticated;

create function public.device_enrollment(p_device_id text,p_token_hash text,p_job_id uuid default null,
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
 elsif p_step in ('tap_card','place_finger','remove_finger','place_again','saving') then
  update public.enrollment_jobs set step=p_step,status='running' where id=job.id;
  return jsonb_build_object('status',200,'jobId',job.id,'ack',false);
 else return jsonb_build_object('status',400,'error','Invalid step'); end if;
 select * into job from public.enrollment_jobs where id=p_job_id;
 return jsonb_build_object('status',200,'jobId',job.id,'state',job.status,'ack',true);
end; $$;
revoke all on function public.device_enrollment(text,text,uuid,text,text,integer,text) from public,anon,authenticated;
grant execute on function public.device_enrollment(text,text,uuid,text,text,integer,text) to service_role;
