-- Manager registration creates an inactive profile until hardware enrollment is complete.
alter table public.employees add column age integer check (age between 1 and 120);
alter table public.employees add column position text check (length(btrim(position)) between 1 and 100);
alter table public.employees alter column rfid_uid drop not null;
alter table public.employees alter column fingerprint_template_id drop not null;
alter table public.employees add constraint active_employee_credentials check (
  status <> 'active' or (rfid_uid is not null and fingerprint_template_id is not null)
);

create table private.employee_registration_audit (
  employee_id text primary key references public.employees(id),
  actor_uid text not null, registered_at timestamptz not null default now()
);
revoke all on private.employee_registration_audit from public, anon, authenticated;

create function public.register_employee(
  p_full_name text, p_age integer, p_position text, p_device_id text, p_employment_type text
) returns public.employees language plpgsql security definer set search_path = '' as $$
declare terminal public.devices; employee public.employees; employee_id text;
begin
  select * into terminal from public.devices where id = p_device_id;
  if not private.valid_firebase_identity() or not exists (
    select 1 from public.user_profiles p where p.firebase_uid = auth.jwt()->>'sub'
      and p.is_active and p.role in ('owner','admin','team_leader')
      and (p.role = 'owner' or terminal.branch_id = any(p.branch_ids))
  ) then raise exception 'You do not have permission to register employees for this terminal.' using errcode = '42501'; end if;
  if terminal.id is null or terminal.status <> 'active' or not exists (
    select 1 from public.branches where id = terminal.branch_id and is_active
  ) then raise exception 'Select an active terminal and branch.' using errcode = '22023'; end if;
  if p_full_name is null or length(btrim(p_full_name)) not between 2 and 120
    or p_age is null or p_age not between 1 and 120
    or p_position is null or length(btrim(p_position)) not between 1 and 100
    or p_employment_type is null or p_employment_type not in ('full_time','part_time')
    then raise exception 'Check the employee name, age, position and employment type.' using errcode = '22023'; end if;
  employee_id := gen_random_uuid()::text;
  insert into public.employees(id,employee_code,full_name,age,position,branch_id,assigned_device_id,status,employment_type)
    values(employee_id,'EMP-' || upper(replace(employee_id,'-','')),btrim(p_full_name),p_age,btrim(p_position),terminal.branch_id,terminal.id,'inactive',p_employment_type)
    returning * into employee;
  insert into private.employee_registration_audit(employee_id,actor_uid) values(employee.id,auth.jwt()->>'sub');
  return employee;
end;
$$;
revoke all on function public.register_employee(text,integer,text,text,text) from public, anon;
grant execute on function public.register_employee(text,integer,text,text,text) to authenticated;
