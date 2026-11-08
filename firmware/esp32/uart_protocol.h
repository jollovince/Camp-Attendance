#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct HardwareScan { char uid[30]; uint16_t fingerprintId; };

inline bool readPositiveNumber(const char *&cursor, uint16_t &value) {
  uint32_t number = 0;
  const char *start = cursor;
  while (*cursor >= '0' && *cursor <= '9') {
    number = number * 10 + (*cursor++ - '0');
    if (number > 65535) return false;
  }
  if (cursor == start || number == 0) return false;
  value = (uint16_t)number;
  return true;
}

inline bool parseHardwareScan(const char *line, HardwareScan &scan) {
  const char prefix[] = "ATTENDANCE_VERIFIED,EMPLOYEE_ID:";
  if (strncmp(line, prefix, sizeof(prefix) - 1)) return false;
  const char *cursor = line + sizeof(prefix) - 1;
  uint16_t employeeId;
  if (!readPositiveNumber(cursor, employeeId)) return false;
  const char card[] = ",RFID_UID:";
  if (strncmp(cursor, card, sizeof(card) - 1)) return false;
  cursor += sizeof(card) - 1;
  size_t length = 0;
  while (*cursor && *cursor != ',') {
    if (length >= sizeof(scan.uid) - 1) return false;
    const char c = *cursor++;
    const bool space = length % 3 == 2;
    if (space ? c != ' ' : !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    scan.uid[length++] = c;
  }
  if (length != 11 && length != 20 && length != 29) return false;
  scan.uid[length] = '\0';
  const char fingerprint[] = ",FINGERPRINT_ID:";
  if (strncmp(cursor, fingerprint, sizeof(fingerprint) - 1)) return false;
  cursor += sizeof(fingerprint) - 1;
  return readPositiveNumber(cursor, scan.fingerprintId) && *cursor == '\0';
}
