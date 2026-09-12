#ifndef MQTT_APP_H
#define MQTT_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "watermeter.h"

#ifdef __cplusplus
extern "C" {
#endif

// Connects to MQTT_BROKER_URI (globals.h) and publishes one retained
// Home Assistant MQTT-discovery config message for the volume sensor.
esp_err_t mqtt_app_start(void);

esp_err_t mqtt_app_wait_connected(uint32_t timeout_ms);

// Publishes the current reading as retained JSON on
// watermeter/<hostname>/state, and "changed" as a separate boolean
// field when volume_m3/date differ from the previous published
// reading (see main.c) — makes it easy to see in HA/logs exactly when
// the tag's snapshot updates.
void mqtt_app_publish_reading(const watermeter_reading_t *reading, bool changed_since_last);

// TEMPORARY diagnostic-only (see watermeter_debug.h) — publishes to
// watermeter/debug/tagdump (not retained, unrelated to the real state/
// availability topics). Always publishes {ts, crc32}; only includes the
// full dump as a hex string when `changed` is true, so routine traffic
// stays tiny. Delete this declaration (and its mqtt_app.c definition,
// and the call site in main.c) once WATERMETER_DEBUG_FULL_DUMP is
// retired.
void mqtt_app_publish_debug_dump(uint32_t crc32, const uint8_t *dump, size_t dump_len, bool changed);

#ifdef __cplusplus
}
#endif

#endif // MQTT_APP_H
