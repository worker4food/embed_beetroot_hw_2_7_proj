#pragma once

#include <stdint.h>
#include <esp_err.h>

#include "config.h"

/* Runs the full connection/auth handshake (protocol doc §3) over an already
 * connected & characteristic-subscribed BLE link: ECDH key exchange, session
 * key derivation, auth-status wake-up, and auto-authentication using
 * cfg->ef_user as the EcoFlow user id.
 *
 * `dev_serial` is the device serial number string parsed from the BLE
 * advertisement's manufacturer data (protocol doc §1).
 *
 * Blocks until authentication succeeds, fails, or times out. Sends outbound
 * frames via river2_ble_write() and reads inbound frames via the BLE
 * notification queue (see ble_client.h).
 */
esp_err_t river2_auth_run(const ecoflow_config_t *cfg, const char *dev_serial);

/* One-shot MD5 (via PSA Crypto), exposed for diagnostics (e.g. logging a
 * fingerprint of cfg->lookup_table) as well as internal use by
 * river2_auth_run(). */
void river2_md5(const uint8_t *data, size_t len, uint8_t out[16]);
