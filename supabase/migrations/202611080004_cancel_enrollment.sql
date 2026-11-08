-- Use the existing failed terminal state so deployed ESP32 firmware can acknowledge cancellation.
create function public.cancel_employee_enrollment(p_job_id uuid) returns public.enrollment_jobs
language plpgsql security definer set search_path = '' as $$
declare job public.enrollment_jobs;
begin
 select * into job from public.enrollment_jobs where id=p_job_id;
 if not found or not private.can_read_branch(job.branch_id) then
  raise exception 'Enrollment access denied.' using errcode='42501';
 end if;
 -- Match the device endpoint lock order, serializing cancellation with completion.
 perform 1 from public.devices where id=job.device_id for update;
 select * into job from public.enrollment_jobs where id=p_job_id for update;
 if job.status in ('queued','running') then
  update public.enrollment_jobs set status='failed',step='failed',error_code='CANCELLED'
   where id=p_job_id returning * into job;
 end if;
 return job;
end; $$;
revoke all on function public.cancel_employee_enrollment(uuid) from public, anon;
grant execute on function public.cancel_employee_enrollment(uuid) to authenticated;
