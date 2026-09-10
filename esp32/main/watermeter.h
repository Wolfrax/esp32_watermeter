// Ties the PN5180/ISO15693 driver together with the meter-specific
// decode (see docs/findings.md) into one read_once() call.
#ifndef WATERMETER_H
#define WATERMETER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "pn5180.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool tag_present;   // a tag answered the inventory poll at all
    bool uid_match;      // and it was specifically the meter's tag
    uint8_t uid[8];       // MSB-first display order
    bool decode_valid;   // volume/date fields parsed successfully
    double volume_m3;
    int year, month, day; // the tag's "today" date field
} watermeter_reading_t;

// Hard-reboots and initializes the PN5180, verifies it responds
// (product version != 0xFF), and enables the RF field. Call once
// after pn5180_init().
esp_err_t watermeter_init(pn5180_t *dev);

// Polls for a tag and, if found, decodes the daily-log record.
// Returns false only on a communication error re-arming the RF field
// (not simply "no tag nearby", which is tag_present=false with a true
// return — the meter reader is expected to sit right next to the
// meter, so "no tag" is itself worth reporting, not an error).
bool watermeter_read(pn5180_t *dev, watermeter_reading_t *out);

#ifdef __cplusplus
}
#endif

#endif // WATERMETER_H
