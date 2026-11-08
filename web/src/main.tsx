// Local development opens the Laragon/USB workspace by default.
const parameters = new URLSearchParams(location.search);
const localhost = ['127.0.0.1', 'localhost'].includes(location.hostname);
if (parameters.get('offline') === '1' || (localhost && parameters.get('mode') !== 'cloud')) {
  if (localhost && location.port !== '8787') location.replace('http://127.0.0.1:8787/');
  else void import('./OfflineApp');
} else {
  void import('./CloudApp');
}
