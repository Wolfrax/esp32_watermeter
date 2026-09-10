#ifndef GLOBALS_H
#define GLOBALS_H

#include "secrets.h" // SSID_STR, WIFI_PW_STR, MQTT_USERNAME, MQTT_PASSWORD — gitignored, see secrets.h.example

#define TAG "WATERMETER"

#define IP_STR_LEN      16
#define HOSTNAME_LEN    32
#define SSID_LEN        33
#define PASSWORD_LEN    65
#define MAC_STR_LEN     18

// WiFi: SSID_STR/WIFI_PW_STR come from secrets.h, seeded into NVS on
// first boot only (see wifi.c). To change after that, either update
// via NVS directly or `idf.py erase-flash` and reflash.

// Plain-text manifest: first whitespace-separated token is the target
// version string, second is the firmware .bin URL. Matches the
// esp32_watertank project's OTA convention.
#define OTA_MANIFEST_URL "http://wolfrax.local:8000/watermeter/manifest.txt"

// mosquitto broker on rpi7 (see Wolfrax/homeassistant-config docs) —
// shares the host network with the homeassistant container, reachable
// as rpi7.local. (Corrected 2026-09-10 — was wrongly set to rpi4.local;
// cross-checked against esp32_watertank/esp32/main/globals.h.)
// MQTT_USERNAME/MQTT_PASSWORD come from secrets.h.
#define MQTT_BROKER_URI  "mqtt://rpi7.local:1883"

// PN5180 <-> ESP32-C6-DevKitC-1 wiring — see ../../docs/wiring.md.
// Confirmed working during hardware bring-up (docs/wiring.md,
// bringup/PN5180_bringup).
#define PN5180_PIN_RST   0
#define PN5180_PIN_NSS   1
#define PN5180_PIN_SCK   2
#define PN5180_PIN_MISO  3
#define PN5180_PIN_MOSI  6
#define PN5180_PIN_BUSY  7
#define PN5180_PIN_CE    10  // PD/CE, hard-reboot control

// The water meter's known tag UID (docs/findings.md), MSB-first
// display order. Readings are only trusted if the UID matches this —
// guards against a stray nearby tag being misread as the meter.
#define METER_UID_INIT { 0xE0, 0x02, 0x24, 0x69, 0x31, 0x38, 0x49, 0xE3 }

// The tag's daily-log record is fixed at blocks 11 (volume) / 12
// (date) as observed during bring-up (docs/findings.md) — see
// watermeter.c for the decode and a fallback scan if this ever shifts.
#define WATERMETER_VOLUME_BLOCK  11
#define WATERMETER_DATE_BLOCK    12

// Poll interval. Set deliberately short for now (not once-daily) to
// empirically characterize when/how often the tag's "today" record
// actually updates — see docs/findings.md "Validated: full read via
// ESP32-C6 + PN5180" for why this isn't yet known to be exactly once
// per day at a fixed time. Revisit once the update cadence is known;
// once-daily polling is almost certainly sufficient long-term.
#define POLL_INTERVAL_MS  (30 * 60 * 1000)  // 30 minutes

extern char ip_str[IP_STR_LEN];
extern char hostname[HOSTNAME_LEN];
extern char current_ssid[SSID_LEN];
extern char ssid[SSID_LEN];
extern char password[PASSWORD_LEN];
extern char macstr[MAC_STR_LEN];

#endif
