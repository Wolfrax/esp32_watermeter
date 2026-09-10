// See watermeter.h. Decode logic matches docs/analyze_dump.py /
// docs/findings.md: a daily-log record is [4-byte LE uint32 volume,
// units 0.0001 m3][4-byte BCD date DD MM YY 00]. The "today" record
// was observed at a fixed location (blocks 11/12) across every
// capture during bring-up; a small scan window is kept as a fallback
// in case that ever shifts (e.g. after the tag fills up and wraps).

#include <string.h>

#include "watermeter.h"
#include "pn5180_iso15693.h"
#include "globals.h"
#include "esp_log.h"

static const uint8_t METER_UID[8] = METER_UID_INIT;

static bool bcd_decimal(uint8_t b, int *out)
{
    int hi = b >> 4, lo = b & 0xF;
    if (hi > 9 || lo > 9) {
        return false;
    }
    *out = hi * 10 + lo;
    return true;
}

static bool decode_at(pn5180_t *dev, const uint8_t uid[8],
                       uint8_t vol_block, uint8_t date_block,
                       watermeter_reading_t *out)
{
    uint8_t vol_bytes[4], date_bytes[4];

    if (pn5180_iso15693_read_single_block(dev, uid, vol_block, vol_bytes, 4) != ISO15693_EC_OK) {
        return false;
    }
    if (pn5180_iso15693_read_single_block(dev, uid, date_block, date_bytes, 4) != ISO15693_EC_OK) {
        return false;
    }

    int d, m, y;
    if (!bcd_decimal(date_bytes[0], &d) || !bcd_decimal(date_bytes[1], &m) ||
        !bcd_decimal(date_bytes[2], &y) || date_bytes[3] != 0) {
        return false;
    }
    if (!(d >= 1 && d <= 31) || !(m >= 1 && m <= 12) || !(y >= 20 && y <= 30)) {
        return false;
    }

    uint32_t vol_raw;
    memcpy(&vol_raw, vol_bytes, 4); // LE, matches ESP32's native byte order

    out->volume_m3 = vol_raw / 10000.0;
    out->year = 2000 + y;
    out->month = m;
    out->day = d;
    out->decode_valid = true;
    return true;
}

esp_err_t watermeter_init(pn5180_t *dev)
{
    pn5180_hard_reboot(dev);
    pn5180_reset(dev);

    uint8_t product_version[2] = {0};
    pn5180_read_eeprom(dev, PN5180_EEPROM_PRODUCT_VERSION, product_version, 2);
    ESP_LOGI(TAG, "PN5180 product version: %d.%d", product_version[1], product_version[0]);

    if (product_version[1] == 0xff) {
        ESP_LOGE(TAG, "PN5180 not responding (product version reads 0xFF) - check wiring");
        return ESP_FAIL;
    }

    if (!pn5180_iso15693_setup_rf(dev)) {
        ESP_LOGE(TAG, "Failed to enable PN5180 RF field");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool watermeter_read(pn5180_t *dev, watermeter_reading_t *out)
{
    memset(out, 0, sizeof(*out));

    uint8_t uid[8]; // LSB first, as returned by getInventory
    iso15693_error_t rc = pn5180_iso15693_get_inventory(dev, uid);
    if (rc == ISO15693_EC_NO_CARD) {
        out->tag_present = false;
        return true;
    }
    if (rc != ISO15693_EC_OK) {
        ESP_LOGW(TAG, "getInventory error, re-arming RF field");
        pn5180_reset(dev);
        pn5180_iso15693_setup_rf(dev);
        return false;
    }

    out->tag_present = true;
    for (int i = 0; i < 8; i++) {
        out->uid[i] = uid[7 - i]; // MSB-first display order
    }
    out->uid_match = (memcmp(out->uid, METER_UID, 8) == 0);

    if (!decode_at(dev, uid, WATERMETER_VOLUME_BLOCK, WATERMETER_DATE_BLOCK, out)) {
        // Fallback: small scan window in case the record location shifts.
        for (int vb = 8; vb <= 20 && !out->decode_valid; vb++) {
            decode_at(dev, uid, vb, vb + 1, out);
        }
    }

    return true;
}
