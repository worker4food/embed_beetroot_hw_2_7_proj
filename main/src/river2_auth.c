#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <psa/crypto.h>

#include "ble_client.h"
#include "river2_auth.h"
#include "river2_ecdh.h"
#include "river2_packet.h"

#define STEP_TIMEOUT_MS 20000

static void ensure_psa_crypto_init(void)
{
    static bool inited = false;
    if (!inited) {
        psa_crypto_init();
        inited = true;
    }
}

void river2_md5(const uint8_t *data, size_t len, uint8_t out[16])
{
    ensure_psa_crypto_init();
    size_t hash_len = 0;
    psa_hash_compute(PSA_ALG_MD5, data, len, out, 16, &hash_len);
}

static esp_err_t import_aes_key(const uint8_t key[16], psa_key_usage_t usage, psa_algorithm_t alg,
                                 mbedtls_svc_key_id_t *out_key_id)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr, usage);
    psa_set_key_algorithm(&attr, alg);
    psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attr, 128);

    return psa_import_key(&attr, key, 16, out_key_id) == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
}

static esp_err_t aes_cbc_encrypt_pkcs7(const river2_session_t *c, const uint8_t *in, size_t in_len,
                                        uint8_t *out, size_t out_cap, size_t *out_len)
{
    ensure_psa_crypto_init();

    mbedtls_svc_key_id_t key_id;
    esp_err_t err = import_aes_key(c->key, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_CBC_PKCS7, &key_id);
    if (err != ESP_OK) {
        return err;
    }

    psa_cipher_operation_t op = psa_cipher_operation_init();
    err = ESP_FAIL;
    size_t total = 0;

    if (psa_cipher_encrypt_setup(&op, key_id, PSA_ALG_CBC_PKCS7) == PSA_SUCCESS &&
        psa_cipher_set_iv(&op, c->iv, 16) == PSA_SUCCESS) {
        size_t update_len = 0, finish_len = 0;
        if (psa_cipher_update(&op, in, in_len, out, out_cap, &update_len) == PSA_SUCCESS &&
            psa_cipher_finish(&op, out + update_len, out_cap - update_len, &finish_len) ==
                PSA_SUCCESS) {
            total = update_len + finish_len;
            err = ESP_OK;
        }
    }
    psa_cipher_abort(&op);
    psa_destroy_key(key_id);

    if (err == ESP_OK) {
        *out_len = total;
    }
    return err;
}

/* Mirrors the reference implementation's lenient decrypt: truncate ciphertext
 * to a whole number of AES blocks, decrypt with no padding, then unpad
 * ourselves if the result carries valid PKCS7 padding. Otherwise return the
 * raw decrypted bytes as-is. Decrypting with PSA_ALG_CBC_NO_PADDING (rather
 * than asking PSA to remove PKCS7 padding) is what makes the fallback
 * possible: PSA_ALG_CBC_PKCS7 would just fail the whole call on bad padding. */
esp_err_t river2_session_decrypt(const river2_session_t *c, const uint8_t *in, size_t in_len,
                                  uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t aligned = in_len - (in_len % 16);
    if (aligned == 0) {
        *out_len = 0;
        return ESP_OK;
    }
    if (out_cap < aligned) {
        return ESP_ERR_INVALID_SIZE;
    }

    ensure_psa_crypto_init();

    mbedtls_svc_key_id_t key_id;
    esp_err_t err = import_aes_key(c->key, PSA_KEY_USAGE_DECRYPT, PSA_ALG_CBC_NO_PADDING, &key_id);
    if (err != ESP_OK) {
        return err;
    }

    psa_cipher_operation_t op = psa_cipher_operation_init();
    err = ESP_FAIL;

    if (psa_cipher_decrypt_setup(&op, key_id, PSA_ALG_CBC_NO_PADDING) == PSA_SUCCESS &&
        psa_cipher_set_iv(&op, c->iv, 16) == PSA_SUCCESS) {
        size_t update_len = 0, finish_len = 0;
        if (psa_cipher_update(&op, in, aligned, out, out_cap, &update_len) == PSA_SUCCESS &&
            psa_cipher_finish(&op, out + update_len, out_cap - update_len, &finish_len) ==
                PSA_SUCCESS) {
            err = ESP_OK;
        }
    }
    psa_cipher_abort(&op);
    psa_destroy_key(key_id);

    if (err != ESP_OK) {
        return err;
    }

    uint8_t pad = out[aligned - 1];
    bool valid_pad = (pad >= 1 && pad <= 16 && pad <= aligned);
    if (valid_pad) {
        for (size_t i = 0; i < pad; i++) {
            if (out[aligned - 1 - i] != pad) {
                valid_pad = false;
                break;
            }
        }
    }
    *out_len = valid_pad ? (aligned - pad) : aligned;
    return ESP_OK;
}

/* Blocks (up to `timeout_ms`) until a frame of `want_frame_type` is available. */
static esp_err_t wait_for_frame(river2_framebuf_t *fb, uint8_t want_frame_type,
                                 const uint8_t **payload_out, size_t *payload_len_out,
                                 uint32_t timeout_ms)
{
    int64_t deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    for (;;) {
        uint8_t frame_type;
        if (river2_framebuf_extract(fb, &frame_type, payload_out, payload_len_out)) {
            if (frame_type == want_frame_type) {
                return ESP_OK;
            }
            ESP_LOGD(__func__, "Skipping frame_type=%u while waiting for %u", frame_type,
                     want_frame_type);
            continue;
        }

        int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0) {
            return ESP_ERR_TIMEOUT;
        }

        uint8_t chunk[RIVER2_BLE_NOTIFY_CHUNK_MAX];
        size_t chunk_len = 0;
        uint32_t remaining_ms = (uint32_t)(remaining_us / 1000) + 1;
        esp_err_t err = river2_ble_wait_notification(chunk, sizeof(chunk), &chunk_len, remaining_ms);
        if (err == ESP_ERR_TIMEOUT) {
            continue; /* loop re-checks the overall deadline */
        }
        if (err != ESP_OK) {
            return err;
        }
        if (!river2_framebuf_append(fb, chunk, chunk_len)) {
            return ESP_ERR_NO_MEM;
        }
    }
}

static esp_err_t send_frame(uint8_t frame_type, const uint8_t *payload, size_t payload_len)
{
    uint8_t frame[600];
    size_t frame_len = river2_outer_build(frame_type, payload, payload_len, frame, sizeof(frame));
    if (frame_len == 0) {
        ESP_LOGE(__func__, "Frame too large to encode (%u bytes payload)", (unsigned)payload_len);
        return ESP_ERR_INVALID_SIZE;
    }
    return river2_ble_write(frame, frame_len);
}

static esp_err_t send_inner_encrypted(const river2_session_t *cipher, uint8_t src, uint8_t dst,
                                       uint8_t cmd_set, uint8_t cmd_id,
                                       const uint8_t *payload, size_t payload_len)
{
    uint8_t inner[300];
    size_t inner_len = river2_inner_build(src, dst, cmd_set, cmd_id, payload, payload_len,
                                          inner, sizeof(inner));
    if (inner_len == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t encrypted[320];
    size_t encrypted_len = 0;
    esp_err_t err = aes_cbc_encrypt_pkcs7(cipher, inner, inner_len, encrypted, sizeof(encrypted),
                                          &encrypted_len);
    if (err != ESP_OK) {
        return err;
    }

    return send_frame(RIVER2_FRAME_DATA, encrypted, encrypted_len);
}

/* Session-key derivation (protocol doc §3.2). Reading each 8-byte group as a
 * little-endian u64 and immediately re-packing it as little-endian is a
 * no-op, so this reduces to a straight concatenation of lookup-table and
 * sRand bytes followed by MD5. */
static esp_err_t derive_session_key(const ecoflow_config_t *cfg, const uint8_t seed[2],
                                     const uint8_t srand[16], uint8_t out_key[16])
{
    int pos = (int)seed[0] * 0x10 + (((int)seed[1] - 1) & 0xFF) * 0x100;
    if (pos < 0 || (size_t)pos + 16 > sizeof(cfg->lookup_table)) {
        ESP_LOGE(__func__, "Session key lookup position %d out of bounds", pos);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[32];
    memcpy(data, &cfg->lookup_table[pos], 16);
    memcpy(data + 16, srand, 16);

    river2_md5(data, sizeof(data), out_key);
    return ESP_OK;
}

static const char *auth_error_name(uint8_t code)
{
    switch (code) {
    case 0x00: return NULL; /* success */
    case 0x01: return "NeedRefreshToken";
    case 0x02: return "DeviceInternalError";
    case 0x03: return "DeviceAlreadyBound";
    case 0x04: return "NeedBindInstallFirst";
    case 0x05: return "AppSendDataError";
    case 0x06: return "WrongKey";
    case 0x07: return "MaximumDevicesError";
    default: return "UnknownError";
    }
}

esp_err_t river2_auth_run(const ecoflow_config_t *cfg, const char *dev_serial,
                           river2_session_t *out_session)
{
    esp_err_t err;
    river2_framebuf_t fb;
    river2_framebuf_init(&fb);

    /* --- Step 1: ECDH key exchange (protocol doc §3.1) --- */
    river2_ecdh_ctx_t *ecdh = NULL;
    uint8_t our_pub[RIVER2_ECDH_PUBKEY_SIZE];
    err = river2_ecdh_gen_keypair(&ecdh, our_pub);
    if (err != ESP_OK) {
        goto done;
    }

    {
        uint8_t payload[2 + RIVER2_ECDH_PUBKEY_SIZE] = {0x01, 0x00};
        memcpy(payload + 2, our_pub, sizeof(our_pub));
        err = send_frame(RIVER2_FRAME_COMMAND, payload, sizeof(payload));
        if (err != ESP_OK) {
            ESP_LOGE(__func__, "Failed to send public key: %s", esp_err_to_name(err));
            goto done;
        }
    }

    river2_session_t interim;
    {
        const uint8_t *reply;
        size_t reply_len;
        err = wait_for_frame(&fb, RIVER2_FRAME_COMMAND, &reply, &reply_len, STEP_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGE(__func__, "Timed out waiting for public key reply");
            goto done;
        }
        if (reply_len < 3 || reply[0] != 0x01) {
            ESP_LOGE(__func__, "Unexpected public key reply (type=%u len=%u)",
                     reply_len ? reply[0] : 0xFF, (unsigned)reply_len);
            err = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }

        uint8_t curve_type = reply[2];
        size_t dev_pub_size;
        switch (curve_type) {
        case 1: dev_pub_size = 52; break;
        case 2: dev_pub_size = 56; break;
        case 3:
        case 4: dev_pub_size = 64; break;
        default: dev_pub_size = 40; break;
        }
        if (dev_pub_size != RIVER2_ECDH_PUBKEY_SIZE) {
            ESP_LOGE(__func__, "Device uses an unsupported ECDH curve (curve_type=%u, size=%u)",
                     curve_type, (unsigned)dev_pub_size);
            err = ESP_ERR_NOT_SUPPORTED;
            goto done;
        }
        if (reply_len < 3 + dev_pub_size) {
            ESP_LOGE(__func__, "Public key reply too short: %u bytes, need %u",
                     (unsigned)reply_len, (unsigned)(3 + dev_pub_size));
            err = ESP_ERR_INVALID_SIZE;
            goto done;
        }

        uint8_t shared_secret[RIVER2_ECDH_COORD_SIZE];
        err = river2_ecdh_shared_secret(ecdh, reply + 3, dev_pub_size, shared_secret);
        if (err != ESP_OK) {
            goto done;
        }

        river2_md5(shared_secret, sizeof(shared_secret), interim.iv);
        memcpy(interim.key, shared_secret, 16);
    }
    river2_ecdh_free(ecdh);
    ecdh = NULL;

    ESP_LOGI(__func__, "ECDH key exchange complete");

    /* --- Step 2: session-key request (protocol doc §3.2) --- */
    river2_session_t session;
    memcpy(session.iv, interim.iv, 16); /* final cipher reuses step-1's iv */
    {
        uint8_t payload[1] = {0x02};
        err = send_frame(RIVER2_FRAME_COMMAND, payload, sizeof(payload));
        if (err != ESP_OK) {
            ESP_LOGE(__func__, "Failed to send session-key request: %s", esp_err_to_name(err));
            goto done;
        }

        const uint8_t *reply;
        size_t reply_len;
        err = wait_for_frame(&fb, RIVER2_FRAME_COMMAND, &reply, &reply_len, STEP_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGE(__func__, "Timed out waiting for session-key reply");
            goto done;
        }
        if (reply_len < 2 || reply[0] != 0x02) {
            ESP_LOGE(__func__, "Unexpected session-key reply (type=%u len=%u)",
                     reply_len ? reply[0] : 0xFF, (unsigned)reply_len);
            err = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }

        uint8_t decrypted[64];
        size_t decrypted_len = 0;
        err = river2_session_decrypt(&interim, reply + 1, reply_len - 1, decrypted,
                                       sizeof(decrypted), &decrypted_len);
        if (err != ESP_OK) {
            goto done;
        }
        if (decrypted_len < 18) {
            ESP_LOGE(__func__, "Decrypted session-key payload too short: %u bytes",
                     (unsigned)decrypted_len);
            err = ESP_ERR_INVALID_SIZE;
            goto done;
        }

        const uint8_t *srand = decrypted;      /* [0:16) */
        const uint8_t *seed = decrypted + 16;  /* [16:18) */
        err = derive_session_key(cfg, seed, srand, session.key);
        if (err != ESP_OK) {
            goto done;
        }
    }

    ESP_LOGI(__func__, "Session key derived");

    /* --- Step 3: auth-status wake-up (protocol doc §3.3) --- */
    {
        err = send_inner_encrypted(&session, RIVER2_ADDR_APP, RIVER2_ADDR_AUTH, 0x35, 0x89,
                                    NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGE(__func__, "Failed to send auth-status wake-up: %s", esp_err_to_name(err));
            goto done;
        }

        for (;;) {
            const uint8_t *reply;
            size_t reply_len;
            err = wait_for_frame(&fb, RIVER2_FRAME_DATA, &reply, &reply_len, STEP_TIMEOUT_MS);
            if (err != ESP_OK) {
                ESP_LOGE(__func__, "Timed out waiting for auth-status reply");
                goto done;
            }

            uint8_t decrypted[256];
            size_t decrypted_len = 0;
            err = river2_session_decrypt(&session, reply, reply_len, decrypted,
                                           sizeof(decrypted), &decrypted_len);
            if (err != ESP_OK) {
                goto done;
            }

            river2_inner_packet_t pkt;
            if (!river2_inner_parse(decrypted, decrypted_len, &pkt)) {
                ESP_LOGW(__func__, "Dropping undecodable frame while awaiting auth-status reply");
                continue;
            }
            if (pkt.src == RIVER2_ADDR_AUTH && pkt.cmd_set == 0x35) {
                break;
            }
            ESP_LOGD(__func__, "Ignoring unrelated packet (src=%#x cmd_set=%#x) during auth-status wait",
                     pkt.src, pkt.cmd_set);
        }
    }

    ESP_LOGI(__func__, "Auth-status wake-up acknowledged");

    /* --- Step 4: auto-authentication (protocol doc §3.4-3.5) --- */
    {
        size_t user_len = strnlen(cfg->ef_user, sizeof(cfg->ef_user));
        size_t serial_len = strlen(dev_serial);
        uint8_t md5_in[USER_ID_SIZE + 20]; /* generous bound for serial_number length */
        if (user_len + serial_len > sizeof(md5_in)) {
            ESP_LOGE(__func__, "user_id + serial_number too long for auth digest buffer");
            err = ESP_ERR_INVALID_SIZE;
            goto done;
        }
        memcpy(md5_in, cfg->ef_user, user_len);
        memcpy(md5_in + user_len, dev_serial, serial_len);

        uint8_t digest[16];
        river2_md5(md5_in, user_len + serial_len, digest);

        uint8_t hex_payload[32];
        for (int i = 0; i < 16; i++) {
            snprintf((char *)&hex_payload[i * 2], 3, "%02X", digest[i]);
        }

        err = send_inner_encrypted(&session, RIVER2_ADDR_APP, RIVER2_ADDR_AUTH, 0x35, 0x86,
                                    hex_payload, sizeof(hex_payload));
        if (err != ESP_OK) {
            ESP_LOGE(__func__, "Failed to send auto-authentication: %s", esp_err_to_name(err));
            goto done;
        }

        for (;;) {
            const uint8_t *reply;
            size_t reply_len;
            err = wait_for_frame(&fb, RIVER2_FRAME_DATA, &reply, &reply_len, STEP_TIMEOUT_MS);
            if (err != ESP_OK) {
                ESP_LOGE(__func__, "Timed out waiting for authentication result");
                goto done;
            }

            uint8_t decrypted[256];
            size_t decrypted_len = 0;
            err = river2_session_decrypt(&session, reply, reply_len, decrypted,
                                           sizeof(decrypted), &decrypted_len);
            if (err != ESP_OK) {
                goto done;
            }

            river2_inner_packet_t pkt;
            if (!river2_inner_parse(decrypted, decrypted_len, &pkt)) {
                ESP_LOGW(__func__, "Dropping undecodable frame while awaiting auth result");
                continue;
            }

            bool is_auth_reply = (pkt.src == RIVER2_ADDR_AUTH && pkt.cmd_set == 0x35 &&
                                   pkt.cmd_id == 0x86);
            if (!is_auth_reply) {
                /* Any other packet arriving here already implies the device
                 * accepted us (protocol doc §3.5). */
                ESP_LOGI(__func__, "Authenticated (first data packet received)");
                err = ESP_OK;
                break;
            }

            uint8_t code = (pkt.payload_len == 1) ? pkt.payload[0] : 0xFF;
            const char *error_name = auth_error_name(code);
            if (pkt.payload_len == 1 && code == 0x00) {
                ESP_LOGI(__func__, "Authenticated");
                err = ESP_OK;
            } else {
                ESP_LOGE(__func__, "Authentication failed: %s (payload=%02x, len=%u)",
                         error_name ? error_name : "UnknownError", code,
                         (unsigned)pkt.payload_len);
                err = ESP_ERR_INVALID_STATE;
            }
            break;
        }
    }

done:
    if (err == ESP_OK) {
        *out_session = session;
    }
    river2_ecdh_free(ecdh);
    river2_framebuf_free(&fb);
    return err;
}

esp_err_t river2_send_command(const river2_session_t *session, uint8_t dst, uint8_t cmd_set,
                               uint8_t cmd_id, const uint8_t *payload, size_t payload_len)
{
    return send_inner_encrypted(session, RIVER2_ADDR_APP, dst, cmd_set, cmd_id, payload, payload_len);
}
