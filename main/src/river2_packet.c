#include <stdlib.h>
#include <string.h>

#include "river2_crc.h"
#include "river2_packet.h"

#define INNER_PREFIX   0xAA
#define INNER_VERSION  0x02
#define INNER_PRODUCT  0x0D /* product_id >= 0 => 0x0D, per reference implementation */
#define INNER_HEADER_LEN 16 /* bytes [0:16) preceding the payload */
#define INNER_OVERHEAD (INNER_HEADER_LEN + 2) /* + trailing crc16 */

#define OUTER_OVERHEAD 8 /* 2 prefix + 1 type + 1 unknown + 2 len + 2 crc16 */

static inline uint16_t rd_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline void wr_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

size_t river2_inner_build(uint8_t src, uint8_t dst, uint8_t cmd_set, uint8_t cmd_id,
                           const uint8_t *payload, size_t payload_len,
                           uint8_t *out, size_t out_cap)
{
    size_t total = INNER_OVERHEAD + payload_len;
    if (out_cap < total) {
        return 0;
    }

    out[0] = INNER_PREFIX;
    out[1] = INNER_VERSION;
    wr_u16le(&out[2], (uint16_t)payload_len);
    out[4] = river2_crc8(out, 4);
    out[5] = INNER_PRODUCT;
    memset(&out[6], 0, 4);  /* seq: opaque passthrough, zero for app-originated packets */
    out[10] = 0x00;
    out[11] = 0x00;
    out[12] = src;
    out[13] = dst;
    out[14] = cmd_set;
    out[15] = cmd_id;
    if (payload_len > 0) {
        memcpy(&out[INNER_HEADER_LEN], payload, payload_len);
    }
    wr_u16le(&out[INNER_HEADER_LEN + payload_len], river2_crc16(out, INNER_HEADER_LEN + payload_len));

    return total;
}

bool river2_inner_parse(const uint8_t *data, size_t len, river2_inner_packet_t *out)
{
    if (len < INNER_OVERHEAD || data[0] != INNER_PREFIX || data[1] != INNER_VERSION) {
        return false;
    }

    uint16_t payload_len = rd_u16le(&data[2]);
    if (len != INNER_OVERHEAD + payload_len) {
        return false;
    }
    if (river2_crc8(data, 4) != data[4]) {
        return false;
    }
    uint16_t crc_expect = river2_crc16(data, len - 2);
    if (rd_u16le(&data[len - 2]) != crc_expect) {
        return false;
    }

    out->src = data[12];
    out->dst = data[13];
    out->cmd_set = data[14];
    out->cmd_id = data[15];
    out->payload = (payload_len > 0) ? &data[INNER_HEADER_LEN] : NULL;
    out->payload_len = payload_len;
    return true;
}

size_t river2_outer_build(uint8_t frame_type, const uint8_t *payload, size_t payload_len,
                           uint8_t *out, size_t out_cap)
{
    size_t total = OUTER_OVERHEAD + payload_len;
    if (out_cap < total) {
        return 0;
    }

    out[0] = 0x5A;
    out[1] = 0x5A;
    out[2] = (uint8_t)(frame_type << 4);
    out[3] = 0x01;
    wr_u16le(&out[4], (uint16_t)(payload_len + 2));
    if (payload_len > 0) {
        memcpy(&out[6], payload, payload_len);
    }
    wr_u16le(&out[6 + payload_len], river2_crc16(out, 6 + payload_len));

    return total;
}

void river2_framebuf_init(river2_framebuf_t *fb)
{
    memset(fb, 0, sizeof(*fb));
}

void river2_framebuf_free(river2_framebuf_t *fb)
{
    free(fb->data);
    free(fb->scratch);
    memset(fb, 0, sizeof(*fb));
}

bool river2_framebuf_append(river2_framebuf_t *fb, const uint8_t *data, size_t len)
{
    if (len == 0) {
        return true;
    }
    if (fb->len + len > fb->cap) {
        size_t new_cap = fb->len + len;
        uint8_t *grown = realloc(fb->data, new_cap);
        if (grown == NULL) {
            return false;
        }
        fb->data = grown;
        fb->cap = new_cap;
    }
    memcpy(&fb->data[fb->len], data, len);
    fb->len += len;
    return true;
}

static void framebuf_consume(river2_framebuf_t *fb, size_t n)
{
    memmove(fb->data, &fb->data[n], fb->len - n);
    fb->len -= n;
}

bool river2_framebuf_extract(river2_framebuf_t *fb, uint8_t *frame_type,
                              const uint8_t **payload, size_t *payload_len)
{
    for (;;) {
        if (fb->len < 2) {
            return false;
        }

        /* Find the 0x5A5A prefix, discarding any garbage before it. */
        size_t start = SIZE_MAX;
        for (size_t i = 0; i + 1 < fb->len; i++) {
            if (fb->data[i] == 0x5A && fb->data[i + 1] == 0x5A) {
                start = i;
                break;
            }
        }
        if (start == SIZE_MAX) {
            /* Keep the last byte in case it's the first half of a split prefix. */
            framebuf_consume(fb, fb->len - 1);
            return false;
        }
        if (start > 0) {
            framebuf_consume(fb, start);
        }

        if (fb->len < 6) {
            return false; /* header incomplete, wait for more data */
        }

        uint16_t payload_field = rd_u16le(&fb->data[4]); /* encrypted payload len + 2 */
        if (payload_field < 2 || payload_field > 4096) {
            /* Corrupt/implausible length: this prefix was spurious, skip it. */
            framebuf_consume(fb, 2);
            continue;
        }

        size_t data_end = 6 + payload_field;
        if (data_end > fb->len) {
            return false; /* frame incomplete, wait for more data */
        }

        uint16_t crc_expect = river2_crc16(fb->data, data_end - 2);
        if (rd_u16le(&fb->data[data_end - 2]) != crc_expect) {
            /* CRC mismatch: spurious prefix match, keep scanning past it. */
            framebuf_consume(fb, 2);
            continue;
        }

        *frame_type = (uint8_t)(fb->data[2] >> 4);
        *payload_len = payload_field - 2;

        /* Copy the payload out to a stable scratch buffer before consuming the
         * frame: framebuf_consume() memmoves the backing store, which would
         * otherwise invalidate a pointer taken directly into it. */
        if (*payload_len > 0) {
            if (*payload_len > fb->scratch_cap) {
                uint8_t *grown = realloc(fb->scratch, *payload_len);
                if (grown == NULL) {
                    return false;
                }
                fb->scratch = grown;
                fb->scratch_cap = *payload_len;
            }
            memcpy(fb->scratch, &fb->data[6], *payload_len);
            *payload = fb->scratch;
        } else {
            *payload = NULL;
        }

        framebuf_consume(fb, data_end);
        return true;
    }
}
