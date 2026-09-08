#pragma once

#include <stddef.h>
#include <stdint.h>

/* CRC8: width=8 poly=0x07 init=0x00, not reflected. Used over inner-packet bytes [0:4]. */
uint8_t river2_crc8(const uint8_t *data, size_t len);

/* CRC16: width=16 poly=0x8005 init=0x0000, reflected in/out (CRC-16/ARC).
 * Used over the outer wrapper and the inner packet, each minus their trailing 2 CRC bytes. */
uint16_t river2_crc16(const uint8_t *data, size_t len);
