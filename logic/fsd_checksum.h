#pragma once
#include <stdint.h>

/* checksum = ((id & 0xFF) + ((id >> 8) & 0xFF) + sum(data[0..len-1])) & 0xFF */
static inline uint8_t tesla_additive_checksum(uint32_t can_id,
                                              const uint8_t* data,
                                              uint8_t len) {
    uint16_t sum = (uint16_t)((can_id & 0xFFu) + ((can_id >> 8) & 0xFFu));
    for (uint8_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFFu);
}
