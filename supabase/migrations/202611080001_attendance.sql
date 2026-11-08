create schema if not exists private;
revoke all on schema private from public, anon, authenticated;

create table public.branches (
  id text primary key, name text not null, is_active boolean not null default true,
  created_at timestamptz not null default now()
);
create table public.user_profiles (
  firebase_uid text primary key,
  role text not null check (role in ('owner','admin','team_leader')),
  branch_ids text[] not null default '{}', is_active boolean not null default true
);
create table public.devices (
  id text primary key check (id ~ '^[A-Za-z0-9_-]{1,80}$'),
  name text not null, branch_id text not null references public.branches(id),
  status text not null check (status in ('active','inactive')),
  last_seen_at timestamptz, firmware_version text
);
create table private.device_credentials (
  device_id text primary key references public.devices(id),
  token_hash text not null check (token_hash ~ '^[a-f0-9]{64}$')
);
create table public.employees (
  id text primary key, employee_code text not null unique, full_name text not null,
  branch_id text not null references public.branches(id),
  assigned_device_id text not null references public.devices(id),
  status text not null check (status in ('active','inactive')),
  employment_type text not null check (employment_type in ('full_time','part_time')),
  rfid_uid text not null,
  fingerprint_template_id integer not null check (fingerprint_template_id between 1 and 65535),
  created_at timestamptz not null default now(),
  unique (assigned_device_id, rfid_uid),
  unique (assigned_device_id, fingerprint_template_id)
);
create table public.attendance_events (
  id text primary key, event_id text not null, device_id text not null references public.devices(id),
  employee_id text not null references public.employees(id), branch_id text not null references public.branches(id),
  rfid_uid text not null, fingerprint_template_id integer not null,
  received_at timestamptz not null default now(),
  result text not null default 'verified' check (result = 'verified'),
  verification_method text not null default 'rfid_and_fingerprint',
  unique (device_id,event_id)
);
create index attendance_branch_received on public.attendance_events(branch_id, received_at desc);

create function private.valid_firebase_identity() returns boolean
language sql stable set search_path = '' as $$
  select coalesce(auth.jwt()->>'iss' = 'https://securetoken.google.com/camp-attendance-d0d37'
    and auth.jwt()->>'aud' = 'camp-attendance-d0d37'
    and length(auth.jwt()->>'sub') > 0, false);
$$;

create function private.can_read_branch(target_branch text) returns boolean
language sql stable security definer set search_path = '' as $$
  select private.valid_firebase_identity() and exists (
    select 1 from public.user_profiles p where p.firebase_uid = auth.jwt()->>'sub'
      and p.is_active and (p.role = 'owner' or target_branch = any(p.branch_ids))
  );
$$;
grant usage on schema private to authenticated;
revoke all on function private.valid_firebase_identity() from public;
revoke all on function private.can_read_branch(text) from public;
grant execute on function private.valid_firebase_identity() to authenticated;
grant execute on function private.can_read_branch(text) to authenticated;

alter table public.user_profiles enable row level security;
alter table public.branches enable row level security;
alter table public.devices enable row level security;
alter table public.employees enable row level security;
alter table public.attendance_events enable row level security;
alter table private.device_credentials enable row level security;

revoke all on public.user_profiles, public.branches, public.devices, public.employees, public.attendance_events from anon, authenticated;
grant select on public.user_profiles, public.branches, public.devices, public.employees, public.attendance_events to authenticated;
create policy own_profile on public.user_profiles for select to authenticated
  using (private.valid_firebase_identity() and firebase_uid = auth.jwt()->>'sub');
create policy branch_read on public.branches for select to authenticated using (private.can_read_branch(id));
create policy device_read on public.devices for select to authenticated using (private.can_read_branch(branch_id));
create policy employee_read on public.employees for select to authenticated using (private.can_read_branch(branch_id));
create policy event_read on public.attendance_events for select to authenticated using (private.can_read_branch(branch_id));

create function public.ingest_device_event(
  p_device_id text, p_token_hash text, p_event_id text, p_rfid_uid text, p_fingerprint_id integer
) returns jsonb language plpgsql security definer set search_path = '' as $$
declare
  terminal public.devices; employee public.employees; previous public.attendance_events;
  receipt text := p_device_id || ':' || p_event_id;
begin
  if p_device_id is null or p_device_id !~ '^[A-Za-z0-9_-]{1,80}$'
    or p_event_id is null or p_event_id !~ '^[A-Za-z0-9_-]{1,80}$'
    or p_fingerprint_id is null or p_fingerprint_id not between 1 and 65535
    or p_rfid_uid is null or p_rfid_uid !~ '^([A-F0-9]{2} ){3}[A-F0-9]{2}(( [A-F0-9]{2}){3}|( [A-F0-9]{2}){6})?$'
    then return jsonb_build_object('status',400,'error','Invalid payload'); end if;
  -- A device lock serializes concurrent retries before the unique event insert.
  select * into terminal from public.devices where id = p_device_id for update;
  if not found or terminal.status <> 'active' or not exists (
    select 1 from private.device_credentials c where c.device_id = p_device_id and c.token_hash = p_token_hash
  ) then return jsonb_build_object('status',401,'error','Unauthorized device'); end if;
  if not exists (select 1 from public.branches b where b.id = terminal.branch_id and b.is_active)
    then return jsonb_build_object('status',403,'error','Inactive branch'); end if;
  select * into previous from public.attendance_events where device_id = p_device_id and event_id = p_event_id;
  if found then
    if previous.rfid_uid <> p_rfid_uid or previous.fingerprint_template_id <> p_fingerprint_id
      then return jsonb_build_object('status',409,'error','Event ID already used'); end if;
    return jsonb_build_object('status',200,'eventId',p_event_id,'receiptId',receipt,'duplicate',true);
  end if;
  select * into employee from public.employees e where e.assigned_device_id = p_device_id and e.rfid_uid = p_rfid_uid;
  if not found or employee.status <> 'active' or employee.branch_id <> terminal.branch_id
    or employee.fingerprint_template_id <> p_fingerprint_id
    then return jsonb_build_object('status',403,'error','Employee mapping rejected'); end if;
  insert into public.attendance_events(id,event_id,device_id,employee_id,branch_id,rfid_uid,fingerprint_template_id)
    values(receipt,p_event_id,p_device_id,employee.id,terminal.branch_id,p_rfid_uid,p_fingerprint_id);
  update public.devices set last_seen_at = now() where id = p_device_id;
  return jsonb_build_object('status',201,'eventId',p_event_id,'receiptId',receipt,'duplicate',false);
end;
$$;
revoke all on function public.ingest_device_event(text,text,text,text,integer) from public, anon, authenticated;
grant execute on function public.ingest_device_event(text,text,text,text,integer) to service_role;

alter publication supabase_realtime add table public.attendance_events, public.employees, public.devices;
