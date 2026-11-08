#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_system.h>
#include <time.h>
#include "secrets.h"
#include "uart_protocol.h"
#include "sd_backup.h"
#include "wifi_setup.h"

HardwareSerial MegaSerial(2);
Preferences storage;
constexpr uint8_t QUEUE_CAPACITY = 20;
struct PendingEvent {
  char eventId[49];
  char uid[30];
  uint16_t fingerprintId;
};
struct DurableQueue {
  uint32_t version;
  uint8_t count;
  PendingEvent events[QUEUE_CAPACITY];
};
DurableQueue pending{};
// Runtime journal markers deliberately stay outside the existing NVS blob:
// queued events written by the deployed firmware remain readable after update.
bool sdArchived[QUEUE_CAPACITY]{};
bool storageReady = false;
char inputLine[192];
size_t inputSize = 0;
bool discardLine = false;
unsigned long nextUpload = 0;
uint32_t retryDelay = 2000;
#include "enrollment_bridge.h"

void readConsole() {
  static char command[24];
  static uint8_t length = 0;
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      command[length] = '\0';
      if (strcmp(command, "SD_STATUS") == 0) printSdBackupStatus();
      length = 0;
    } else if (length < sizeof(command) - 1) command[length++] = ch;
    else length = 0;
  }
}

bool persistQueue(const DurableQueue &next) {
  if (!storageReady || storage.putBytes("queue-v1", &next, sizeof(next)) != sizeof(next)) {
    Serial.println("ERROR: Could not persist queue. Event has NOT been acknowledged.");
    return false;
  }
  pending = next;
  return true;
}

void acceptLine(const char *line) {
  if (handleEnrollmentLine(line)) return;
  HardwareScan scan{};
  if (!parseHardwareScan(line, scan)) {
    Serial.println("UART rejected: malformed verification message.");
    return;
  }
  if (pending.count >= QUEUE_CAPACITY) {
    Serial.println("ERROR: Queue full. Scan NOT saved. Stop scanning until upload recovers.");
    MegaSerial.println("QUEUE_FULL");
    return;
  }
  DurableQueue next = pending;
  PendingEvent &event = next.events[next.count];
  snprintf(event.eventId, sizeof(event.eventId), "%08lx%08lx%08lx%08lx",
    (unsigned long)esp_random(), (unsigned long)esp_random(),
    (unsigned long)esp_random(), (unsigned long)esp_random());
  strlcpy(event.uid, scan.uid, sizeof(event.uid));
  event.fingerprintId = scan.fingerprintId;
  next.count++;
  if (!persistQueue(next)) return;
  sdArchived[pending.count - 1] = writeSdBackup(event.eventId, event.uid, event.fingerprintId, "captured");
  Serial.printf("UART verified event saved. Pending: %u. Waiting for server receipt.\n", pending.count);
  // The current Mega does not consume these transport acknowledgements.
  MegaSerial.print("QUEUED,"); MegaSerial.println(event.eventId);
}

void readMega() {
  while (MegaSerial.available()) {
    const char ch = (char)MegaSerial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      if (!discardLine && inputSize) { inputLine[inputSize] = '\0'; acceptLine(inputLine); }
      inputSize = 0;
      discardLine = false;
    } else if (!discardLine) {
      if (inputSize < sizeof(inputLine) - 1) inputLine[inputSize++] = ch;
      else { discardLine = true; Serial.println("UART rejected: line too long."); }
    }
  }
}

void uploadOldest() {
  if (!pending.count) return;
  if (!sdArchived[0]) {
    const PendingEvent &head = pending.events[0];
    sdArchived[0] = writeSdBackup(head.eventId, head.uid, head.fingerprintId, "pending_recovered");
    if (!sdArchived[0]) { nextUpload = millis() + 10000; return; }
  }
  if (!pending.count || WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) return;
  if (strlen(INGEST_URL) == 0 || strlen(DEVICE_TOKEN) < 32) return;
  if (!String(INGEST_URL).startsWith("https://")) {
    Serial.println("ERROR: Ingestion URL must use verified HTTPS.");
    nextUpload = millis() + 60000;
    return;
  }
  const PendingEvent &event = pending.events[0];
  String body = "{\"version\":1,\"deviceId\":\"" + String(DEVICE_ID) +
    "\",\"eventId\":\"" + event.eventId + "\",\"rfidUid\":\"" + event.uid +
    "\",\"fingerprintTemplateId\":" + String(event.fingerprintId) + "}";
  NetworkClientSecure tls;
  tls.useBuiltinCACertBundle();
  tls.setHandshakeTimeout(8);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(tls, INGEST_URL)) { nextUpload = millis() + retryDelay; return; }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + DEVICE_TOKEN);
  const int status = http.POST(body);
  const String receipt = status == 200 || status == 201 ? http.getString() : "";
  http.end();
  if ((status == 200 || status == 201) && receipt.indexOf(String(event.eventId)) >= 0) {
    if (!writeSdBackup(event.eventId, event.uid, event.fingerprintId, "server_confirmed")) {
      // The server may have committed; retain the same ID for an idempotent retry.
      nextUpload = millis() + 10000;
      return;
    }
    DurableQueue next = pending;
    for (uint8_t i = 1; i < next.count; i++) next.events[i - 1] = next.events[i];
    memset(&next.events[--next.count], 0, sizeof(PendingEvent));
    if (persistQueue(next)) {
      for (uint8_t i = 1; i < QUEUE_CAPACITY; i++) sdArchived[i - 1] = sdArchived[i];
      sdArchived[QUEUE_CAPACITY - 1] = false;
      Serial.printf("SERVER CONFIRMED: event stored (HTTP %d). Pending: %u\n", status, pending.count);
      MegaSerial.println("SERVER_CONFIRMED");
    }
    retryDelay = 2000;
  } else {
    Serial.printf("Upload not confirmed (HTTP %d). Event retained with same ID.\n", status);
    if (status >= 400 && status < 500 && status != 408 && status != 429) {
      Serial.println("Configuration/mapping error: fix server or device config; queue retained.");
      retryDelay = 60000;
    } else retryDelay = min(retryDelay * 2, (uint32_t)60000);
  }
  nextUpload = millis() + retryDelay;
}

void setup() {
  Serial.begin(115200);
  MegaSerial.setRxBufferSize(8192);
  MegaSerial.begin(9600, SERIAL_8N1, 16, 17);
  pending.version = 1;
  storageReady = storage.begin("camp-attend", false);
  if (storageReady && storage.isKey("queue-v1")) {
    DurableQueue saved{};
    const size_t bytes = storage.getBytes("queue-v1", &saved, sizeof(saved));
    if (bytes == sizeof(saved) && saved.version == 1 && saved.count <= QUEUE_CAPACITY) pending = saved;
    else { storageReady = false; Serial.println("ERROR: Queue corrupt; storage preserved for recovery."); }
  }
  Serial.println("ESP32 ATTENDANCE BRIDGE READY - UART RX16/TX17 at 9600");
  Serial.printf("Restored pending events: %u\n", pending.count);
  if (!strlen(INGEST_URL)) Serial.println("CLOUD NOT CONFIGURED: UART events will be saved locally only.");
  beginSdBackup();
  beginEnrollmentBridge();
  beginWifiConnection();
  configTime(0, 0, "time.google.com", "pool.ntp.org");
}

void loop() {
  readMega();
  readConsole();
  pollWifiConnection();
  if ((long)(millis() - nextUpload) >= 0) uploadOldest();
  pollEnrollment();
  delay(5);
}

