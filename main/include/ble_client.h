#pragma once

#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>

/* BLE lifecycle events, delivered as bits on the owner task's own task
 * notification (xTaskNotify(..., eSetBits)) rather than a separate event
 * group, since only one task ever owns/waits on them. Drain with
 * xTaskNotifyWait(0, UINT32_MAX, &bits, ...). */
#define BLE_EVT_HOST_SYNC     (1u << 0)
#define BLE_EVT_CONNECTED     (1u << 1)
#define BLE_EVT_CONNECT_ERROR (1u << 2)
#define BLE_EVT_DISCONNECTED  (1u << 3)

/* Brings up the NimBLE host. Non-blocking: the calling task becomes the
 * owner task and is notified with BLE_EVT_HOST_SYNC once synced. Call once,
 * before river2_ble_start_connect(). */
esp_err_t river2_ble_init(void);

/* Kicks off a connection to `target_addr` (6 raw address bytes, public
 * address type assumed). Non-blocking: resolves the GATT characteristic
 * pair (protocol doc §2) and subscribes to notifications in the background,
 * notifying the owner task with BLE_EVT_CONNECTED or BLE_EVT_CONNECT_ERROR. */
esp_err_t river2_ble_start_connect(const uint8_t target_addr[6]);

/* Writes a complete outer-wrapper frame to the resolved write characteristic
 * (write-with-response, per protocol doc §2/§4.1). */
esp_err_t river2_ble_write(const uint8_t *data, size_t len);

/* Blocks up to `timeout_ms` for the next BLE notification chunk (buffered on
 * its own queue, since a task notification can't carry a variable-length
 * payload). Returns ESP_ERR_TIMEOUT if none arrived. A logical frame can
 * span multiple chunks/calls; see river2_framebuf_* in river2_packet.h. */
esp_err_t river2_ble_wait_notification(uint8_t *out_buf, size_t out_cap, size_t *out_len,
                                        uint32_t timeout_ms);
