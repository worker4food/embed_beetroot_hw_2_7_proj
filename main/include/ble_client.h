#pragma once

#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>

/* Brings up the NimBLE host and blocks until it has synced with the
 * controller. Must be called once before river2_ble_connect(). */
esp_err_t river2_ble_init(void);

/* Scans for a River2-family device advertising manufacturer data (company id
 * 0xB5B5, protocol doc §1) whose BLE address matches `target_addr` (6 raw
 * address bytes; the address *type* isn't known ahead of time, so matching is
 * done on the address value alone and the discovered advertisement's own type
 * is used for the subsequent connect), connects to it, resolves whichever
 * GATT characteristic pair is present (protocol doc §2), and subscribes to
 * notifications.
 *
 * On success, fills `out_serial` (NUL-terminated) with the device's
 * advertised serial number and `out_encrypt_type` with its capability-flags
 * derived encrypt_type (protocol doc §1). Blocks up to `timeout_ms`. */
esp_err_t river2_ble_connect(const uint8_t target_addr[6],
                              char *out_serial, size_t out_serial_cap,
                              uint8_t *out_encrypt_type, uint32_t timeout_ms);

/* Writes a complete outer-wrapper frame to the resolved write characteristic
 * (write-with-response, per protocol doc §2/§4.1). */
esp_err_t river2_ble_write(const uint8_t *data, size_t len);

/* Blocks up to `timeout_ms` for the next BLE notification chunk. Returns
 * ESP_ERR_TIMEOUT if none arrived in time. A logical frame can be split
 * across multiple chunks/calls, see river2_framebuf_* in river2_packet.h. */
esp_err_t river2_ble_wait_notification(uint8_t *out_buf, size_t out_cap, size_t *out_len,
                                        uint32_t timeout_ms);
