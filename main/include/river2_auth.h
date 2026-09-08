#pragma once

#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>

#include "config.h"

/* AES-128-CBC key+iv negotiated by river2_auth_run() (protocol doc §3).
 * Outlives the handshake so the caller can keep decrypting telemetry
 * frames (§5) with river2_session_decrypt() after authentication. */
typedef struct {
    uint8_t key[16];
    uint8_t iv[16];
} river2_session_t;

/* Runs the full connection/auth handshake (protocol doc §3) over an already
 * connected & characteristic-subscribed BLE link: ECDH key exchange, session
 * key derivation, auth-status wake-up, and auto-authentication using
 * cfg->ef_user as the EcoFlow user id. On success, fills `out_session` with
 * the negotiated key/iv.
 *
 * `dev_serial` is the device serial number string (cfg->ef_serial).
 *
 * Blocks until authentication succeeds, fails, or times out. Sends outbound
 * frames via river2_ble_write() and reads inbound frames via the BLE
 * notification queue (see ble_client.h).
 */
esp_err_t river2_auth_run(const ecoflow_config_t *cfg, const char *dev_serial,
                           river2_session_t *out_session);

/* Decrypts one AES-CBC payload under `session` (protocol doc §3, lenient
 * PKCS7 unpadding per §4.1). For decoding telemetry frames after
 * river2_auth_run() returns. */
esp_err_t river2_session_decrypt(const river2_session_t *session, const uint8_t *in, size_t in_len,
                                  uint8_t *out, size_t out_cap, size_t *out_len);

/* One-shot MD5 (via PSA Crypto), exposed for diagnostics (e.g. logging a
 * fingerprint of cfg->lookup_table) as well as internal use by
 * river2_auth_run(). */
void river2_md5(const uint8_t *data, size_t len, uint8_t out[16]);

/* Sends a command to the device under an authenticated `session`: builds,
 * encrypts, and writes the inner packet. `dst`/`cmd_set`/`cmd_id`/payload
 * come from the command table in protocol doc §6. Call after
 * river2_auth_run() succeeds. */
esp_err_t river2_send_command(const river2_session_t *session, uint8_t dst, uint8_t cmd_set,
                               uint8_t cmd_id, const uint8_t *payload, size_t payload_len);
