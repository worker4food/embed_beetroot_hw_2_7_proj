/*
 * ECDH key exchange on SECP160r1 for the River2Pro handshake (protocol doc §3.1),
 * built on the vendored micro-ecc library (components/micro-ecc), which supports
 * secp160r1 as a first-class curve, unlike this ESP-IDF's mbedtls, whose
 * curve list only goes down to secp192r1/secp256k1.
 */

#include <stdbool.h>
#include <stdlib.h>

#include <esp_log.h>
#include <esp_random.h>

#include "uECC.h"

#include "river2_ecdh.h"

#define RIVER2_ECDH_PRIVKEY_SIZE 21 /* secp160r1 order needs 21 bytes; see uECC.h */

static const char *TAG = "river2_ecdh";

struct river2_ecdh_ctx {
    uint8_t priv[RIVER2_ECDH_PRIVKEY_SIZE];
};

static int rng_cb(uint8_t *dest, unsigned size)
{
    esp_fill_random(dest, size);
    return 1;
}

static void ensure_rng(void)
{
    static bool rng_set = false;
    if (!rng_set) {
        uECC_set_rng(rng_cb);
        rng_set = true;
    }
}

esp_err_t river2_ecdh_gen_keypair(river2_ecdh_ctx_t **out_ctx,
                                   uint8_t out_pubkey[RIVER2_ECDH_PUBKEY_SIZE])
{
    ensure_rng();

    river2_ecdh_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (!uECC_make_key(out_pubkey, ctx->priv, uECC_secp160r1())) {
        ESP_LOGE(TAG, "uECC_make_key failed");
        free(ctx);
        return ESP_FAIL;
    }

    *out_ctx = ctx;
    return ESP_OK;
}

esp_err_t river2_ecdh_shared_secret(river2_ecdh_ctx_t *ctx,
                                     const uint8_t *peer_pubkey, size_t peer_pubkey_len,
                                     uint8_t out_secret[RIVER2_ECDH_COORD_SIZE])
{
    if (peer_pubkey_len != RIVER2_ECDH_PUBKEY_SIZE) {
        ESP_LOGE(TAG, "Unexpected peer public key size %u (want %u)",
                 (unsigned)peer_pubkey_len, (unsigned)RIVER2_ECDH_PUBKEY_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    if (!uECC_valid_public_key(peer_pubkey, uECC_secp160r1())) {
        ESP_LOGE(TAG, "Device public key is not a valid secp160r1 point");
        return ESP_ERR_INVALID_ARG;
    }

    if (!uECC_shared_secret(peer_pubkey, ctx->priv, out_secret, uECC_secp160r1())) {
        ESP_LOGE(TAG, "uECC_shared_secret failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void river2_ecdh_free(river2_ecdh_ctx_t *ctx)
{
    free(ctx);
}
