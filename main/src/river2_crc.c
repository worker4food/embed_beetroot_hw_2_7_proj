#include <stdbool.h>

#include "river2_crc.h"

static uint8_t s_crc8_table[256];
static uint16_t s_crc16_table[256];
static bool s_tables_built = false;

static void build_tables(void)
{
    for (int i = 0; i < 256; i++) {
        uint8_t crc8 = (uint8_t)i;
        for (int b = 0; b < 8; b++) {
            crc8 = (crc8 & 0x80) ? (uint8_t)((crc8 << 1) ^ 0x07) : (uint8_t)(crc8 << 1);
        }
        s_crc8_table[i] = crc8;

        uint16_t crc16 = (uint16_t)i;
        for (int b = 0; b < 8; b++) {
            crc16 = (crc16 & 1) ? (uint16_t)((crc16 >> 1) ^ 0xA001) : (uint16_t)(crc16 >> 1);
        }
        s_crc16_table[i] = crc16;
    }
    s_tables_built = true;
}

uint8_t river2_crc8(const uint8_t *data, size_t len)
{
    if (!s_tables_built) {
        build_tables();
    }
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc = s_crc8_table[crc ^ data[i]];
    }
    return crc;
}

uint16_t river2_crc16(const uint8_t *data, size_t len)
{
    if (!s_tables_built) {
        build_tables();
    }
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < len; i++) {
        crc = (uint16_t)(s_crc16_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8));
    }
    return crc;
}
