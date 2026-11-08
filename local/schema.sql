CREATE TABLE IF NOT EXISTS administrators (
 email VARCHAR(190) PRIMARY KEY, salt CHAR(32) NOT NULL, password_hash CHAR(64) NOT NULL
);
CREATE TABLE IF NOT EXISTS employees (
 id CHAR(36) PRIMARY KEY, full_name VARCHAR(120) NOT NULL, age INT NOT NULL,
 position VARCHAR(100) NOT NULL, employment_type VARCHAR(20) NOT NULL,
 status VARCHAR(20) NOT NULL DEFAULT 'inactive', rfid_uid VARCHAR(29) UNIQUE,
 fingerprint_template_id INT UNIQUE, created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS enrollment_jobs (
 id CHAR(36) PRIMARY KEY, employee_id CHAR(36) NOT NULL,
 status VARCHAR(20) NOT NULL DEFAULT 'running', step VARCHAR(30) NOT NULL DEFAULT 'waiting_for_mega',
 error_code VARCHAR(64), created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
 FOREIGN KEY (employee_id) REFERENCES employees(id)
);
CREATE TABLE IF NOT EXISTS attendance_events (
 id CHAR(36) PRIMARY KEY, employee_id CHAR(36) NOT NULL, device_id VARCHAR(40) NOT NULL DEFAULT 'usb-mega',
 received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY (employee_id) REFERENCES employees(id)
);
