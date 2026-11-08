import {initializeApp} from 'firebase/app';
import {getAuth} from 'firebase/auth';
const env = import.meta.env;
export const configured = Boolean(env.VITE_FIREBASE_API_KEY && env.VITE_FIREBASE_APP_ID);
const app = configured ? initializeApp({apiKey:env.VITE_FIREBASE_API_KEY,authDomain:env.VITE_FIREBASE_AUTH_DOMAIN,projectId:env.VITE_FIREBASE_PROJECT_ID,appId:env.VITE_FIREBASE_APP_ID}) : null;
export const auth = app ? getAuth(app) : null;
