#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Outer wrapper frame types (protocol doc §4.1). */
#define RIVER2_FRAME_COMMAND 0x00 /* unencrypted handshake command/reply */
#define RIVER2_FRAME_DATA    0x01 /* AES-CBC encrypted inner packet */

/* Board addresses (protocol doc §4.4). */
#define RIVER2_ADDR_APP  0x21
#define RIVER2_ADDR_AUTH 0x35
#define RIVER2_ADDR_PD   0x02

/* PD heartbeat (protocol doc §5.1): src=RIVER2_ADDR_PD, cmd_set/cmd_id below. */
#define RIVER2_CMDSET_PD_HEARTBEAT 0x20
#define RIVER2_CMDID_PD_HEARTBEAT  0x02

/* Builds an inner packet (wire version 2, protocol doc §4.2):
 * prefix/version/len/crc8 header, seq/zero/src/dst/cmd_set/cmd_id, payload, crc16.
 * `out` must have room for 16 + payload_len + 2 bytes. Returns the encoded length,
 * or 0 if `out_cap` is too small. */
size_t river2_inner_build(uint8_t src, uint8_t dst, uint8_t cmd_set, uint8_t cmd_id,
                           const uint8_t *payload, size_t payload_len,
                           uint8_t *out, size_t out_cap);

typedef struct {
    uint8_t src;
    uint8_t dst;
    uint8_t cmd_set;
    uint8_t cmd_id;
    const uint8_t *payload; /* points into the buffer passed to river2_inner_parse() */
    size_t payload_len;
} river2_inner_packet_t;

/* Validates and parses a complete inner packet (prefix, both CRCs, exact length).
 * `data`/`len` must span exactly one packet. Returns true and fills `out` on success. */
bool river2_inner_parse(const uint8_t *data, size_t len, river2_inner_packet_t *out);

/* Builds the outer wrapper (protocol doc §4.1) around `payload` (the caller has
 * already encrypted it if frame_type == RIVER2_FRAME_DATA). Returns the encoded
 * length, or 0 if `out_cap` is too small. */
size_t river2_outer_build(uint8_t frame_type, const uint8_t *payload, size_t payload_len,
                           uint8_t *out, size_t out_cap);

/* Streaming reassembler for the outer wrapper: BLE notifications can split a
 * frame across multiple deliveries, and can also carry more than one frame per
 * delivery. Feed raw notification bytes in with river2_framebuf_append(), then
 * drain complete frames with river2_framebuf_extract(). */
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    uint8_t *scratch;    /* holds the most recently extracted payload */
    size_t scratch_cap;
} river2_framebuf_t;

void river2_framebuf_init(river2_framebuf_t *fb);
void river2_framebuf_free(river2_framebuf_t *fb);

/* Appends notification bytes to the buffer. Returns false on allocation failure. */
bool river2_framebuf_append(river2_framebuf_t *fb, const uint8_t *data, size_t len);

/* Extracts one complete, CRC-valid frame from the buffer, if available.
 * `payload` is left pointing at an internal buffer valid until the next
 * append/extract call. Returns false if no complete frame is buffered yet. */
bool river2_framebuf_extract(river2_framebuf_t *fb, uint8_t *frame_type,
                              const uint8_t **payload, size_t *payload_len);
