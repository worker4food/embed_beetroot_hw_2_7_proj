#pragma once

#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>

/* Brings up the NimBLE host and blocks until it has synced with the
 * controller. Must be called once before river2_ble_connect(). */
esp_err_t river2_ble_init(void);

/* Connects directly to `target_addr` (6 raw address bytes, public address
 * type assumed), resolves whichever GATT characteristic pair is present
 * (protocol doc §2), and subscribes to notifications. Blocks up to
 * `timeout_ms`. */
esp_err_t river2_ble_connect(const uint8_t target_addr[6], uint32_t timeout_ms);

/* Writes a complete outer-wrapper frame to the resolved write characteristic
 * (write-with-response, per protocol doc §2/§4.1). */
esp_err_t river2_ble_write(const uint8_t *data, size_t len);

/* Blocks up to `timeout_ms` for the next BLE notification chunk. Returns
 * ESP_ERR_TIMEOUT if none arrived in time, or ESP_ERR_INVALID_STATE if a
 * non-notification internal event was received instead (should not happen
 * once river2_ble_connect() has returned). A logical frame can be split
 * across multiple chunks/calls, see river2_framebuf_* in river2_packet.h. */
esp_err_t river2_ble_wait_notification(uint8_t *out_buf, size_t out_cap, size_t *out_len,
                                        uint32_t timeout_ms);
