#pragma once

#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>

#define RIVER2_ECDH_COORD_SIZE 20                        /* secp160r1 field element */
#define RIVER2_ECDH_PUBKEY_SIZE (2 * RIVER2_ECDH_COORD_SIZE) /* raw X||Y, no 0x04 prefix */

typedef struct river2_ecdh_ctx river2_ecdh_ctx_t;

/* Generates an ephemeral secp160r1 keypair. `out_pubkey` receives our raw
 * public key (X||Y, 40 bytes, no format prefix) to send to the device.
 * Returns a heap-allocated context that must be freed with river2_ecdh_free(). */
esp_err_t river2_ecdh_gen_keypair(river2_ecdh_ctx_t **out_ctx,
                                   uint8_t out_pubkey[RIVER2_ECDH_PUBKEY_SIZE]);

/* Computes the ECDH shared secret (raw X coordinate, big-endian, zero-padded
 * to RIVER2_ECDH_COORD_SIZE bytes) from our private key and the device's raw
 * public key (X||Y, `peer_pubkey_len` bytes total, no prefix). */
esp_err_t river2_ecdh_shared_secret(river2_ecdh_ctx_t *ctx,
                                     const uint8_t *peer_pubkey, size_t peer_pubkey_len,
                                     uint8_t out_secret[RIVER2_ECDH_COORD_SIZE]);

void river2_ecdh_free(river2_ecdh_ctx_t *ctx);
