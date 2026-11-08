"""Loopback-only offline admin: Laragon MySQL + USB Mega. No cloud APIs."""
import argparse
import hashlib
import hmac
import json
import mimetypes
import re
import secrets
import threading
import time
import uuid
from datetime import date, datetime
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit, unquote

import pymysql
import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parent.parent
PRIVATE = ROOT / '.local'
CONFIG = {'host': '127.0.0.1', 'port': 3306, 'user': 'root', 'password': '', 'database': 'camp_attendance_offline'}
if (PRIVATE / 'offline-config.json').exists():
    CONFIG.update(json.loads((PRIVATE / 'offline-config.json').read_text()))
SESSIONS = {}
LOGIN_ATTEMPTS = []
AUTH_LOCK = threading.Lock()


def database(use_database=True):
    options = dict(CONFIG)
    if not use_database:
        options.pop('database')
    return pymysql.connect(**options, charset='utf8mb4', autocommit=False,
                           cursorclass=pymysql.cursors.DictCursor, connect_timeout=5,
                           init_command="SET time_zone = '+00:00'")


def query(sql, values=()):
    with database() as connection:
        with connection.cursor() as cursor:
            cursor.execute(sql, values)
            rows = cursor.fetchall()
        connection.commit()
        return rows


def initialize():
    name = CONFIG['database']
    if not re.fullmatch(r'[a-zA-Z0-9_]+', name):
        raise ValueError('Invalid local database name')
    with database(False) as connection:
        with connection.cursor() as cursor:
            cursor.execute(f'CREATE DATABASE IF NOT EXISTS `{name}` CHARACTER SET utf8mb4')
        connection.commit()
    with database() as connection:
        with connection.cursor() as cursor:
            for statement in (ROOT / 'local/schema.sql').read_text().split(';'):
                if statement.strip():
                    cursor.execute(statement)
        connection.commit()
    if not query('SELECT email FROM administrators LIMIT 1'):
        source = PRIVATE / 'administrator.json'
        if not source.exists():
            PRIVATE.mkdir(exist_ok=True)
            source = PRIVATE / 'offline-administrator.json'
            if not source.exists():
                source.write_text(json.dumps({'email': 'admin@camp.local', 'password': secrets.token_urlsafe(16)}, indent=2))
        credentials = json.loads(source.read_text())
        salt = secrets.token_hex(16)
        hashed = hashlib.pbkdf2_hmac('sha256', credentials['password'].encode(), bytes.fromhex(salt), 200000).hex()
        query('INSERT INTO administrators(email,salt,password_hash) VALUES(%s,%s,%s)',
              (credentials['email'], salt, hashed))
        print(f'Local admin initialized. Use the credentials in {source.relative_to(ROOT)}')
    # An interrupted USB session must not remain stuck after restarting the PC server.
    query("UPDATE enrollment_jobs SET status='failed',step='failed',error_code='SERVER_RESTARTED' WHERE status='running'")


class UsbBridge:
    def __init__(self):
        self.lock = threading.RLock()
        self.port = None
        self.ready = False
        self.busy_id = None
        self.ack = False
        self.started = 0.0
        self.logs = []

    def snapshot(self):
        with self.lock:
            return {'port': self.port.port if self.port and self.port.is_open else None,
                    'ready': self.ready, 'busy': bool(self.busy_id), 'logs': list(self.logs)}

    def connect(self, name):
        available = {item.device for item in list_ports.comports()}
        if name not in available:
            raise ValueError('Choose an available USB port')
        with self.lock:
            if self.busy_id:
                raise ValueError('Wait for the reader to finish before reconnecting USB')
            if self.port:
                self.port.close()
            self.ready = False
            self.logs = []
            self.port = serial.Serial(name, 9600, timeout=0.1, write_timeout=2)
            threading.Thread(target=self.read, args=(self.port,), daemon=True).start()

    def read(self, port):
        buffer = b''
        try:
            while port.is_open:
                buffer += port.read(port.in_waiting or 1)
                while b'\n' in buffer:
                    line, buffer = buffer.split(b'\n', 1)
                    self.handle_line(line.decode('utf-8', errors='replace').strip())
                if len(buffer) > 2048:
                    buffer = b''
                with self.lock:
                    if self.busy_id and time.monotonic() - self.started > (150 if self.ack else 30):
                        query("UPDATE enrollment_jobs SET status='failed',step='failed',error_code=%s WHERE id=%s AND status='running'",
                              ('HARDWARE_TIMEOUT' if self.ack else 'MEGA_NOT_RESPONDING', self.busy_id))
                        self.busy_id = None
                        self.ready = False
        except (serial.SerialException, OSError):
            with self.lock:
                self.ready = False
                self.logs.append('USB disconnected. Reconnect the board.')
                if self.busy_id:
                    query("UPDATE enrollment_jobs SET status='failed',step='failed',error_code='USB_DISCONNECTED' WHERE id=%s AND status='running'", (self.busy_id,))
                    self.busy_id = None
        except Exception as error:
            with self.lock:
                self.ready = False
                self.logs.append(f'Local hardware processing failed: {type(error).__name__}. Reconnect USB.')

    def start(self, employee):
        with self.lock:
            if not self.port or not self.port.is_open or not self.ready or self.busy_id:
                raise ValueError('Connect the Mega USB and wait for SYSTEM READY. Close Arduino Serial Monitor first.')
            with database() as connection:
                with connection.cursor() as cursor:
                    cursor.execute('SELECT * FROM employees WHERE id=%s FOR UPDATE', (employee,))
                    row = cursor.fetchone()
                    if not row or row['status'] != 'inactive':
                        raise ValueError('Choose an employee pending enrollment')
                    job = str(uuid.uuid4())
                    cursor.execute('INSERT INTO enrollment_jobs(id,employee_id) VALUES(%s,%s)', (job, employee))
                connection.commit()
            self.busy_id, self.started, self.ack, self.ready = job, time.monotonic(), False, False
            try:
                self.port.write(f'ENROLL,{job}\n'.encode())
            except (serial.SerialException, OSError):
                query("UPDATE enrollment_jobs SET status='failed',step='failed',error_code='USB_DISCONNECTED' WHERE id=%s", (job,))
                self.busy_id = None
                raise ValueError('Could not send command. Reconnect USB.')
            return job

    def cancel(self, job):
        with self.lock:
            query("UPDATE enrollment_jobs SET status='cancelled',step='cancelled' WHERE id=%s AND status='running'", (job,))
            # Keep the reader busy until the current capture finishes. Late results cannot activate it.

    def handle_line(self, line):
        with self.lock:
            self.logs = (self.logs + [line])[-30:]
            if line == 'SYSTEM READY':
                self.ready = True
            parts = line.split(',')
            if len(parts) >= 3 and parts[0].startswith('ENROLL_') and parts[1] == self.busy_id:
                kind, job, value = parts[:3]
                if kind == 'ENROLL_ACK' and value == 'received':
                    self.ack = True
                elif kind == 'ENROLL_STEP' and value in ('tap_card', 'place_finger', 'remove_finger', 'place_again', 'saving'):
                    self.ack = True
                    query("UPDATE enrollment_jobs SET step=%s WHERE id=%s AND status='running'", (value, job))
                elif kind == 'ENROLL_FAIL':
                    query("UPDATE enrollment_jobs SET status='failed',step='failed',error_code=%s WHERE id=%s AND status='running'", (value[:64], job))
                    self.busy_id = None
                elif kind == 'ENROLL_DONE' and len(parts) == 4 and re.fullmatch(r'(?:[A-F0-9]{2} ){3}(?:[A-F0-9]{2})(?:(?: [A-F0-9]{2}){3}|(?: [A-F0-9]{2}){6})?', value):
                    self.complete(job, value, int(parts[3]))
                    self.busy_id = None
            match = re.fullmatch(r'ATTENDANCE_VERIFIED,EMPLOYEE_ID:\d+,RFID_UID:([A-F0-9 ]+),FINGERPRINT_ID:(\d+)', line)
            if match:
                rows = query("SELECT id FROM employees WHERE rfid_uid=%s AND fingerprint_template_id=%s AND status='active'", match.groups())
                if rows:
                    query('INSERT INTO attendance_events(id,employee_id) VALUES(%s,%s)', (str(uuid.uuid4()), rows[0]['id']))

    def complete(self, job, uid, finger):
        if not 2 <= finger <= 65535:
            raise ValueError('Invalid fingerprint ID')
        with database() as connection:
            try:
                with connection.cursor() as cursor:
                    cursor.execute('SELECT * FROM enrollment_jobs WHERE id=%s FOR UPDATE', (job,))
                    row = cursor.fetchone()
                    if row and row['status'] == 'running':
                        cursor.execute("UPDATE employees SET rfid_uid=%s,fingerprint_template_id=%s,status='active' WHERE id=%s AND status='inactive'", (uid, finger, row['employee_id']))
                        cursor.execute("UPDATE enrollment_jobs SET status='completed',step='completed' WHERE id=%s", (job,))
                connection.commit()
            except pymysql.IntegrityError:
                connection.rollback()
                query("UPDATE enrollment_jobs SET status='failed',step='failed',error_code='MAPPING_CONFLICT' WHERE id=%s AND status='running'", (job,))


BRIDGE = UsbBridge()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # Never log submitted credentials or cookies.

    def respond(self, value, status=200, cookie=None):
        payload = json.dumps(value, default=lambda x: x.isoformat() if isinstance(x, (date, datetime)) else str(x)).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        if cookie:
            self.send_header('Set-Cookie', cookie)
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def authorized(self):
        cookies = SimpleCookie()
        try:
            cookies.load(self.headers.get('Cookie', ''))
        except Exception:
            return False
        token = cookies.get('camp_local')
        with AUTH_LOCK:
            return bool(token and SESSIONS.get(token.value, 0) > time.time())

    def do_GET(self):
        if not self.valid_host():
            return self.respond({'error': 'Invalid host'}, 403)
        route = urlsplit(self.path).path
        if route == '/api/local/session':
            return self.respond({'authenticated': self.authorized()})
        if route.startswith('/api/local/'):
            if not self.authorized():
                return self.respond({'error': 'Sign in to the local administrator account'}, 401)
            try:
                if route == '/api/local/state':
                    return self.respond({'employees': query('SELECT * FROM employees ORDER BY full_name'),
                                         'jobs': query('SELECT * FROM enrollment_jobs ORDER BY created_at DESC LIMIT 50'),
                                         'events': query('SELECT a.*,e.full_name FROM attendance_events a JOIN employees e ON e.id=a.employee_id ORDER BY received_at DESC LIMIT 200'),
                                         'hardware': BRIDGE.snapshot(),
                                         'ports': [{'id': p.device, 'name': p.description} for p in list_ports.comports()]})
                return self.respond({'error': 'Not found'}, 404)
            except Exception:
                return self.respond({'error': 'Local database unavailable. Start MySQL in Laragon.'}, 503)
        if route == '/':
            self.send_response(302)
            self.send_header('Location', '/index.html?offline=1')
            self.end_headers()
            return
        target = (ROOT / 'web/dist' / unquote(route).lstrip('/')).resolve()
        if not target.is_relative_to((ROOT / 'web/dist').resolve()) or not target.is_file():
            return self.respond({'error': 'File not found. Run npm.cmd --prefix web run build first.'}, 404)
        payload = target.read_bytes()
        self.send_response(200)
        self.send_header('Content-Type', mimetypes.guess_type(target)[0] or 'application/octet-stream')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def valid_host(self):
        return self.headers.get('Host') in (f'127.0.0.1:{self.server.server_port}', f'localhost:{self.server.server_port}')

    def do_POST(self):
        if not self.valid_host() or self.headers.get('Origin') not in (f'http://127.0.0.1:{self.server.server_port}', f'http://localhost:{self.server.server_port}'):
            return self.respond({'error': 'Local browser requests only'}, 403)
        if self.headers.get('Content-Type') != 'application/json':
            return self.respond({'error': 'Expected JSON'}, 400)
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 4096:
                raise ValueError('Invalid request size')
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError('Expected object')
            route = urlsplit(self.path).path
            if route == '/api/local/login':
                with AUTH_LOCK:
                    LOGIN_ATTEMPTS[:] = [t for t in LOGIN_ATTEMPTS if t > time.time() - 60]
                    if len(LOGIN_ATTEMPTS) >= 10:
                        return self.respond({'error': 'Too many attempts. Wait one minute.'}, 429)
                    LOGIN_ATTEMPTS.append(time.time())
                rows = query('SELECT * FROM administrators WHERE email=%s', (str(body.get('email', '')),))
                salt = bytes.fromhex(rows[0]['salt']) if rows else bytes(16)
                hashed = hashlib.pbkdf2_hmac('sha256', str(body.get('password', '')).encode(), salt, 200000).hex()
                if not rows or not hmac.compare_digest(hashed, rows[0]['password_hash']):
                    return self.respond({'error': 'Invalid local email or password'}, 401)
                token = secrets.token_urlsafe(32)
                with AUTH_LOCK:
                    SESSIONS[token] = time.time() + 8 * 3600
                return self.respond({'ok': True}, cookie=f'camp_local={token}; HttpOnly; SameSite=Strict; Path=/; Max-Age=28800')
            if not self.authorized():
                return self.respond({'error': 'Sign in first'}, 401)
            if route == '/api/local/logout':
                cookie = SimpleCookie(self.headers.get('Cookie', ''))
                with AUTH_LOCK:
                    if cookie.get('camp_local'):
                        SESSIONS.pop(cookie['camp_local'].value, None)
                return self.respond({'ok': True}, cookie='camp_local=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0')
            if route == '/api/local/connect':
                BRIDGE.connect(str(body.get('port', '')))
            elif route == '/api/local/employees':
                name, position = str(body.get('full_name', '')).strip(), str(body.get('position', '')).strip()
                age, employment = int(body.get('age', 0)), body.get('employment_type')
                if not 2 <= len(name) <= 120 or not 1 <= len(position) <= 100 or not 1 <= age <= 120 or employment not in ('full_time', 'part_time'):
                    raise ValueError('Enter valid employee details')
                query('INSERT INTO employees(id,full_name,age,position,employment_type) VALUES(%s,%s,%s,%s,%s)', (str(uuid.uuid4()), name, age, position, employment))
            elif route == '/api/local/enroll':
                return self.respond({'job': BRIDGE.start(str(body.get('employee_id', '')))})
            elif route == '/api/local/cancel':
                BRIDGE.cancel(str(body.get('job_id', '')))
            else:
                return self.respond({'error': 'Not found'}, 404)
            self.respond({'ok': True})
        except (ValueError, TypeError, serial.SerialException) as error:
            self.respond({'error': str(error)}, 400)
        except pymysql.MySQLError:
            self.respond({'error': 'Local database error. Check MySQL in Laragon.'}, 503)
        except Exception:
            self.respond({'error': 'Local request failed'}, 500)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8787)
    args = parser.parse_args()
    initialize()
    print(f'Offline admin: http://127.0.0.1:{args.port}/')
    print(f'Laragon database: {CONFIG["database"]}. Close Arduino Serial Monitor before connecting USB.')
    ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()


if __name__ == '__main__':
    try:
        main()
    except pymysql.MySQLError:
        raise SystemExit('Cannot connect to Laragon MySQL. Start MySQL and check .local/offline-config.json if a password is configured.')
