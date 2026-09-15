// Full-tag-dump helper (added 2026-09-12 as a temporary investigation,
// kept permanently from 2026-09-15 — see docs/findings.md "Validated:
// full read via ESP32-C6 + PN5180").
//
// The known daily-log record (globals.h WATERMETER_*_BLOCK) is
// confirmed to update exactly once a day. Several of the tag's other
// byte ranges are still undecoded (docs/findings.md "Not yet
// decoded") — this module reads the tag's *entire* 512-byte memory so
// those can be tracked over time (see main.c's call site: only run
// once/day, when the known record itself rolls over), independent of
// the normal decode path in watermeter.c (which is untouched).
// Feeds docs/analyze_dump.py --diff and the persistent logger on rpi7
// (/var/log/watermeter-tagdump.log).
#ifndef WATERMETER_DEBUG_H
#define WATERMETER_DEBUG_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "pn5180.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATERMETER_DEBUG_DUMP_BLOCKS 128
#define WATERMETER_DEBUG_DUMP_BYTES  (WATERMETER_DEBUG_DUMP_BLOCKS * 4) // 512

// Reads all 128 ISO15693 blocks (the tag's full memory) into `out`.
// `uid` must be in the same LSB-first order pn5180_iso15693_get_inventory()
// returns. Returns false on the first block read failure (out is left
// partially filled in that case).
bool watermeter_debug_dump_full(pn5180_t *dev, const uint8_t uid[8],
                                 uint8_t out[WATERMETER_DEBUG_DUMP_BYTES]);

// Plain CRC32 (not cryptographic, just needs to reliably flag any byte
// change) so we can cheaply tell "did anything change since last cycle"
// without publishing all 512 bytes every time.
uint32_t watermeter_debug_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // WATERMETER_DEBUG_H
