#ifndef OTA_H
#define OTA_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// True if the currently running image is a freshly-flashed OTA update that
// hasn't been confirmed healthy yet (ESP_OTA_IMG_PENDING_VERIFY).
bool ota_is_pending_verify(void);

// Call once WiFi is confirmed working. If this boot is a pending OTA image,
// marks it valid so the bootloader won't roll it back on a future crash/reset.
void ota_mark_valid_if_pending(void);

// Fetches OTA_MANIFEST_URL (version + firmware URL), and if the manifest's
// version differs from the running image's version, downloads and flashes
// it into the inactive OTA slot, then reboots. No-op (with a log line) on
// any fetch/parse/flash failure — never blocks or crashes the caller.
void ota_check_and_update(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_H
