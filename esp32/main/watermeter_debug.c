// See watermeter_debug.h.

#include "watermeter_debug.h"
#include "pn5180_iso15693.h"

bool watermeter_debug_dump_full(pn5180_t *dev, const uint8_t uid[8],
                                 uint8_t out[WATERMETER_DEBUG_DUMP_BYTES])
{
    for (int block = 0; block < WATERMETER_DEBUG_DUMP_BLOCKS; block++) {
        if (pn5180_iso15693_read_single_block(dev, uid, block, out + (block * 4), 4) != ISO15693_EC_OK) {
            return false;
        }
    }
    return true;
}

uint32_t watermeter_debug_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}
