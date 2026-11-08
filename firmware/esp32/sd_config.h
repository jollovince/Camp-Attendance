#pragma once

// SD backup is deferred by user request. Keep cloud uploads independent of SD.
// The existing persistent ESP32 NVS queue remains enabled.
#ifndef CAMP_SD_BACKUP_ENABLED
#define CAMP_SD_BACKUP_ENABLED 0
#endif

constexpr int SD_BACKUP_CS = 13;
constexpr int SD_BACKUP_SCK = 18;
constexpr int SD_BACKUP_MISO = 19;
constexpr int SD_BACKUP_MOSI = 23;
