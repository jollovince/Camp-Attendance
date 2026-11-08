import {createClient} from '@supabase/supabase-js';
import {auth} from './firebase';
const env=import.meta.env;
export const dataConfigured=Boolean(env.VITE_SUPABASE_URL&&env.VITE_SUPABASE_PUBLISHABLE_KEY);
export const supabase=dataConfigured?createClient(env.VITE_SUPABASE_URL,env.VITE_SUPABASE_PUBLISHABLE_KEY,{
  accessToken:async()=>await auth?.currentUser?.getIdToken()??null
}):null;
