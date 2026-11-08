#pragma once
#include "sd_config.h"

#if CAMP_SD_BACKUP_ENABLED
#include <SPI.h>
#include <SD.h>

namespace AttendanceBackup {
bool mounted = false;
unsigned long nextMountAttempt = 0;
const char *const logPath = "/attendance.csv";
const char *const header = "event_id,device_id,rfid_uid,fingerprint_template_id,record_type,recorded_at_utc\r\n";

bool mount() {
  if (mounted) return true;
  if ((long)(millis() - nextMountAttempt) < 0) return false;
  nextMountAttempt = millis() + 10000;
  SD.end();
  // Explicit ESP32 pins; they do not change Mega SPI or UART RX16/TX17.
  SPI.begin(SD_BACKUP_SCK, SD_BACKUP_MISO, SD_BACKUP_MOSI, SD_BACKUP_CS);
  // Never format a card automatically.
  if (!SD.begin(SD_BACKUP_CS, SPI, 1000000, "/sd", 5, false) || SD.cardType() == CARD_NONE) {
    Serial.println("SD BACKUP UNAVAILABLE: pending scans stay in ESP32 flash; cloud upload paused.");
    return false;
  }
  mounted = true;
  Serial.println("SD backup mounted. Journal: /attendance.csv");
  return true;
}

bool fail() {
  mounted = false;
  nextMountAttempt = millis() + 10000;
  Serial.println("SD WRITE FAILED: event retained in ESP32 flash; check card and storage space.");
  return false;
}

// Journal entries share a durable event ID. Recovery/retry may repeat rows;
// they must never be counted as separate attendance events.
bool append(const char *eventId, const char *uid, uint16_t fingerprintId, const char *recordType) {
  if (!mount()) return false;
  String line = String(eventId) + "," + DEVICE_ID + "," + uid + "," +
    String(fingerprintId) + "," + recordType + ",";
  const time_t now = time(nullptr);
  if (now >= 1700000000) {
    struct tm utc{};
    gmtime_r(&now, &utc);
    char text[25];
    strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    line += text;
  } // Empty timestamp means the clock was not synchronized; never invent a time.
  line += "\r\n";
  File file = SD.open(logPath, FILE_APPEND);
  if (!file) return fail();
  const size_t previousSize = file.size();
  bool ok = true;
  if (!previousSize) ok = file.print(header) == strlen(header);
  file.close();

  // Preserve an interrupted partial line, then start the new row cleanly.
  if (ok && previousSize) {
    File read = SD.open(logPath, FILE_READ);
    if (!read || !read.seek(previousSize - 1)) { if (read) read.close(); return fail(); }
    const bool needsNewline = read.read() != '\n';
    read.close();
    if (needsNewline) {
      File repair = SD.open(logPath, FILE_APPEND);
      if (!repair) return fail();
      ok = repair.print("\r\n") == 2;
      repair.flush();
      repair.close();
      Serial.println("SD: preserved an incomplete previous row. Verify CSV after unexpected power loss.");
    }
  }
  if (!ok) return fail();
  file = SD.open(logPath, FILE_APPEND);
  if (!file) return fail();
  const size_t start = file.size();
  ok = file.print(line) == line.length();
  file.flush();
  ok = ok && file.getWriteError() == 0;
  file.close();
  if (!ok) return fail();

  // Read back the actual appended bytes before allowing cloud dequeue.
  File verify = SD.open(logPath, FILE_READ);
  if (!verify || verify.size() != start + line.length() || !verify.seek(start)) {
    if (verify) verify.close();
    return fail();
  }
  for (size_t i = 0; i < line.length(); i++) {
    if (verify.read() != (uint8_t)line[i]) { verify.close(); return fail(); }
  }
  verify.close();
  return true;
}
} // namespace AttendanceBackup
#endif

inline void beginSdBackup() {
#if CAMP_SD_BACKUP_ENABLED
  AttendanceBackup::mount();
#else
  Serial.println("SD backup disabled. Using persistent ESP32 flash queue and cloud upload.");
#endif
}

inline bool writeSdBackup(const char *eventId, const char *uid, uint16_t fingerprintId, const char *recordType) {
#if CAMP_SD_BACKUP_ENABLED
  return AttendanceBackup::append(eventId, uid, fingerprintId, recordType);
#else
  (void)eventId; (void)uid; (void)fingerprintId; (void)recordType;
  return true;
#endif
}

// Read-only USB diagnostic: verify the actual CSV without removing the card.
inline void printSdBackupStatus() {
#if CAMP_SD_BACKUP_ENABLED
  if (!AttendanceBackup::mount()) return;
  Serial.printf("SD CARD CAPACITY: %llu MB\n", (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
  File file = SD.open(AttendanceBackup::logPath, FILE_READ);
  if (!file) { Serial.println("SD CSV not created yet. Waiting for a real scan."); return; }
  const size_t size = file.size();
  Serial.printf("SD CSV SIZE: %u bytes\n", (unsigned int)size);
  if (size > 2048) {
    file.seek(size - 2048);
    while (file.available() && file.read() != '\n') {}
  }
  Serial.println("SD_CSV_BEGIN");
  while (file.available()) Serial.write(file.read());
  Serial.println("SD_CSV_END");
  file.close();
#else
  Serial.println("SD backup disabled in this firmware.");
#endif
}
