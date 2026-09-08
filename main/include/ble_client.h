#pragma once

#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include "app_events.h"

/* Brings up the NimBLE host. Non-blocking: sets BLE_EVT_HOST_SYNC in
 * `events` once synced. Call once, before river2_ble_start_connect(). */
esp_err_t river2_ble_init(EventGroupHandle_t events);

/* Kicks off a connection to `target_addr` (6 raw address bytes, public
 * address type assumed). Non-blocking: resolves the GATT characteristic
 * pair (protocol doc §2) and subscribes to notifications in the background,
 * setting BLE_EVT_CONNECTED or BLE_EVT_CONNECT_ERROR. */
esp_err_t river2_ble_start_connect(const uint8_t target_addr[6]);

/* Writes a complete outer-wrapper frame to the resolved write characteristic
 * (write-with-response, per protocol doc §2/§4.1). */
esp_err_t river2_ble_write(const uint8_t *data, size_t len);

#define RIVER2_BLE_NOTIFY_CHUNK_MAX 256

/* Blocks up to `timeout_ms` for the next BLE notification chunk. Returns
 * ESP_ERR_TIMEOUT if none arrived. A logical frame can span multiple
 * chunks/calls; see river2_framebuf_* in river2_packet.h. */
esp_err_t river2_ble_wait_notification(uint8_t *out_buf, size_t out_cap, size_t *out_len,
                                        uint32_t timeout_ms);
