// ISO15693 commands on top of pn5180.h, ported from
// PN5180ISO15693.cpp — only getInventory/readSingleBlock/setupRF,
// which is all the watermeter reader needs.
#ifndef PN5180_ISO15693_H
#define PN5180_ISO15693_H

#include <stdint.h>
#include "pn5180.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ISO15693_EC_NO_CARD = -1,
    ISO15693_EC_OK = 0,
    ISO15693_EC_ERROR = 1, // any nonzero chip-reported error code, collapsed
} iso15693_error_t;

// Loads the ISO15693 RF config and turns the RF field on. Call once
// after pn5180_reset().
bool pn5180_iso15693_setup_rf(pn5180_t *dev);

// UID is returned LSB-first (index 0 = LSB), matching the tag's own
// byte order over the air. Reverse it for the usual MSB-first display
// order used in docs/findings.md.
iso15693_error_t pn5180_iso15693_get_inventory(pn5180_t *dev, uint8_t uid[8]);

iso15693_error_t pn5180_iso15693_read_single_block(pn5180_t *dev, const uint8_t uid[8],
                                                     uint8_t block_no, uint8_t *block_data,
                                                     uint8_t block_size);

#ifdef __cplusplus
}
#endif

#endif // PN5180_ISO15693_H
