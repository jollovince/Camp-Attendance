import http.client
import json
import os
import threading
import unittest
import uuid
from unittest.mock import patch

import server


class FakeUsb:
    is_open = True
    port = 'TEST-USB'

    def __init__(self):
        self.writes = []

    def write(self, value):
        self.writes.append(value)


class OfflineIntegration(unittest.TestCase):
    def test_offline_login_enrollment_cancellation_and_attendance(self):
        name = f'camp_attendance_offline_test_{os.getpid()}'
        original = server.CONFIG.copy()
        server.CONFIG['database'] = name
        httpd = None
        try:
            server.initialize()
            server.BRIDGE = server.UsbBridge()
            httpd = server.ThreadingHTTPServer(('127.0.0.1', 0), server.Handler)
            threading.Thread(target=httpd.serve_forever, daemon=True).start()
            address = f'http://127.0.0.1:{httpd.server_port}'
            cookie = ''

            def request(path, body=None, origin=None):
                connection = http.client.HTTPConnection('127.0.0.1', httpd.server_port, timeout=10)
                headers = {'Cookie': cookie}
                if body is not None:
                    headers.update({'Content-Type': 'application/json', 'Origin': origin or address})
                connection.request('GET' if body is None else 'POST', '/api/local/' + path,
                                   None if body is None else json.dumps(body), headers)
                response = connection.getresponse()
                result = response.status, json.loads(response.read()), response.getheader('Set-Cookie')
                connection.close()
                return result

            self.assertEqual(request('state')[0], 401)
            self.assertEqual(request('login', {}, 'https://example.com')[0], 403)
            salt = '11' * 16
            hashed = server.hashlib.pbkdf2_hmac('sha256', b'test-password', bytes.fromhex(salt), 200000).hex()
            server.query('INSERT INTO administrators(email,salt,password_hash) VALUES(%s,%s,%s)', ('test@local.test', salt, hashed))
            status, _, session_cookie = request('login', {'email': 'test@local.test', 'password': 'test-password'})
            self.assertEqual(status, 200)
            self.assertIn('HttpOnly', session_cookie)
            cookie = session_cookie.split(';')[0]
            self.assertEqual(request('employees', {'full_name': 'Bad', 'age': 0})[0], 400)

            def employee(name):
                self.assertEqual(request('employees', {'full_name': name, 'age': 24, 'position': 'Barista', 'employment_type': 'full_time'})[0], 200)
                return next(row['id'] for row in request('state')[1]['employees'] if row['full_name'] == name)

            first = employee('Offline test employee')
            self.assertEqual(request('enroll', {'employee_id': first})[0], 400)
            fake = FakeUsb()
            server.BRIDGE.port, server.BRIDGE.ready = fake, True
            status, started, _ = request('enroll', {'employee_id': first})
            self.assertEqual(status, 200)
            job = started['job']
            self.assertEqual(fake.writes, [f'ENROLL,{job}\n'.encode()])
            server.BRIDGE.handle_line(f'ENROLL_STEP,{uuid.uuid4()},tap_card')
            self.assertEqual(server.query('SELECT step FROM enrollment_jobs WHERE id=%s', (job,))[0]['step'], 'waiting_for_mega')
            server.BRIDGE.handle_line(f'ENROLL_ACK,{job},received')
            server.BRIDGE.handle_line(f'ENROLL_STEP,{job},tap_card')
            self.assertTrue(server.BRIDGE.ack)
            server.BRIDGE.handle_line(f'ENROLL_DONE,{job},11 22 33 44,2')
            server.BRIDGE.handle_line('SYSTEM READY')
            row = server.query('SELECT * FROM employees WHERE id=%s', (first,))[0]
            self.assertEqual((row['status'], row['fingerprint_template_id']), ('active', 2))
            server.BRIDGE.handle_line('ATTENDANCE_VERIFIED,EMPLOYEE_ID:1,RFID_UID:11 22 33 44,FINGERPRINT_ID:3')
            self.assertEqual(len(server.query('SELECT * FROM attendance_events')), 0)
            server.BRIDGE.handle_line('ATTENDANCE_VERIFIED,EMPLOYEE_ID:1,RFID_UID:11 22 33 44,FINGERPRINT_ID:2')
            self.assertEqual(len(server.query('SELECT * FROM attendance_events')), 1)

            second = employee('Cancelled test employee')
            second_job = request('enroll', {'employee_id': second})[1]['job']
            self.assertEqual(request('cancel', {'job_id': second_job})[0], 200)
            self.assertTrue(server.BRIDGE.busy_id)
            self.assertEqual(request('enroll', {'employee_id': second})[0], 400)
            server.BRIDGE.handle_line(f'ENROLL_DONE,{second_job},55 66 77 88,3')
            server.BRIDGE.handle_line('SYSTEM READY')
            self.assertEqual(server.query('SELECT status FROM employees WHERE id=%s', (second,))[0]['status'], 'inactive')
            third_job = request('enroll', {'employee_id': second})[1]['job']
            server.BRIDGE.handle_line(f'ENROLL_FAIL,{third_job},CARD_TIMEOUT')
            server.BRIDGE.handle_line('SYSTEM READY')
            self.assertIsNone(server.BRIDGE.busy_id)
            # Data remains available when a new server session initializes.
            server.initialize()
            self.assertEqual(len(server.query('SELECT * FROM attendance_events')), 1)
            self.assertEqual(request('logout', {})[0], 200)
            self.assertEqual(request('state')[0], 401)
            with patch('server.serial.Serial', side_effect=AssertionError('Must not open unknown ports')):
                with self.assertRaises(ValueError):
                    server.BRIDGE.connect('NOT-A-REAL-PORT')
        finally:
            if httpd:
                httpd.shutdown()
                httpd.server_close()
            # Delete only this explicitly named, isolated test database.
            self.assertTrue(name.startswith('camp_attendance_offline_test_'))
            with server.database(False) as connection:
                with connection.cursor() as cursor:
                    cursor.execute(f'DROP DATABASE IF EXISTS `{name}`')
                connection.commit()
            server.CONFIG.clear()
            server.CONFIG.update(original)


if __name__ == '__main__':
    unittest.main()
