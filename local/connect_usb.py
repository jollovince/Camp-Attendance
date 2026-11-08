"""Connect the offline admin to the Mega without printing login credentials."""
import argparse
import http.cookiejar
import json
from pathlib import Path
import time
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM23')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
source = root / '.local/administrator.json'
if not source.exists():
    source = root / '.local/offline-administrator.json'
credentials = json.loads(source.read_text())
base = 'http://127.0.0.1:8787/'
client = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))


def request(route, body=None):
    return json.load(client.open(urllib.request.Request(base + 'api/local/' + route,
        data=None if body is None else json.dumps(body).encode(),
        headers={'Content-Type': 'application/json', 'Origin': base.rstrip('/')}), timeout=10))


request('login', credentials)
request('connect', {'port': args.port})
for attempt in range(30):
    hardware = request('state')['hardware']
    if hardware['ready']:
        print(f"USB verified: {hardware['port']} SYSTEM READY")
        print('\n'.join(hardware['logs']))
        break
    time.sleep(0.5)
else:
    print('\n'.join(hardware['logs']))
    raise SystemExit('USB opened, but Mega SYSTEM READY was not received. Check hardware power and firmware.')
