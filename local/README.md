# Offline admin with Laragon MySQL and USB Mega

This is a separate local mode. It needs no internet, Firebase login, Supabase, or ESP32 Wi-Fi at runtime. Laragon MySQL must be running. The Python server serves the admin at `http://127.0.0.1:8787/` and talks directly to the Mega USB port at 9600 baud. Laragon Apache is optional. No cloud migrations are needed.

The offline admin now uses the original branded dashboard layout: sidebar, Overview metrics, Live attendance, Employees, Devices, Connection & setup, date/search filters and CSV export. Localhost development opens this mode by default and redirects to port 8787. To explicitly open the retained cloud frontend on the Vite server, use `?mode=cloud`.

Verified on this workstation: USB firmware upload to Mega COM23 succeeded, the local server connected to COM23, and real boot logs confirmed RC522 initialization, AS608 detection, three existing fingerprint templates, and SYSTEM READY. No new physical enrollment or attendance capture was performed during this verification. Keep Arduino Serial Monitor closed while the local server owns COM23. USB logs are visible under Connection & setup or Devices.

1. Upload `firmware/arduino-mega/arduino-mega.ino` to the Mega. This version accepts enrollment commands on USB as well as ESP32 Serial1. Do not open cloud enrollment at the same time. No ESP32 update is required for USB mode.
2. In Laragon, start MySQL. Defaults are `127.0.0.1:3306`, user `root`, empty password. If your settings differ, create ignored `.local/offline-config.json` with the appropriate `host`, `port`, `user`, `password`, and optional `database` fields.
3. Install Python dependencies once while internet is available: `python -m pip install -r local/requirements.txt`.
4. Build the local website: `npm.cmd --prefix web run build`.
5. Run `powershell -ExecutionPolicy Bypass -File local/start.ps1` from the project folder. Keep this terminal open.
6. Open `http://127.0.0.1:8787/`. The server initializes the separate `camp_attendance_offline` database. On the first run it copies the existing local administrator email/password into a salted password hash in MySQL. Use the credentials in `.local/administrator.json`. Later local login works without Firebase. If that file is absent, the server generates `.local/offline-administrator.json` instead.
7. Close Arduino Serial Monitor. Choose the Mega USB port in the admin and click **Connect USB**. Opening USB may reset the Mega; wait for **SYSTEM READY**. The serial logs are shown inside the admin.
8. Save an employee, select **Enroll RFID & fingerprint**, start, and follow the card/two-scan prompts. Use a new card and finger; preexisting sensor templates and EEPROM mappings are preserved. Keep USB connected for attendance capture.

The database is visible in Laragon's database manager, HeidiSQL, Adminer, or phpMyAdmin if installed: host `127.0.0.1`, database `camp_attendance_offline`. It has `administrators`, `employees`, `enrollment_jobs`, and `attendance_events`. No phpMyAdmin installation is required to run the app. Back up this database together with the hardware mapping records. Cloud employees are not imported or synchronized automatically.

Cancel prevents late USB results from activating the employee. It does not interrupt the current sensor capture; wait until **SYSTEM READY** before starting another request. Closing the local server during enrollment marks the request failed on its next startup. Reconnecting USB may reset the Mega. Fingerprints already saved in hardware are never automatically deleted.

Verify the software with `python -m unittest discover -s local -p 'test_*.py'` and `npm.cmd --prefix web run build`. Physical card/fingerprint capture and power-cycle recovery must still be tested on the board.
