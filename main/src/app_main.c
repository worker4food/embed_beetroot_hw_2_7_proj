
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_check.h>

#include "ble_client.h"
#include "config.h"
#include "river2_auth.h"

ecoflow_config_t cfg;

#define DEVICE_SERIAL_BUF_SIZE 32
#define BLE_CONNECT_TIMEOUT_MS 30000

#define NOTIFICATION_SINK_BUF_SIZE 512
#define NOTIFICATION_SINK_STACK_SIZE 2048
#define NOTIFICATION_SINK_POLL_TIMEOUT_MS 60000

/* Once authenticated, the device keeps streaming heartbeat/telemetry frames
 * (protocol doc §5) that this project doesn't parse. Nothing else drains
 * river2_ble_wait_notification()'s queue after app_main() returns, so this
 * discards them to keep the link healthy instead of letting the queue fill up
 * and log "Notification queue full" warnings indefinitely. */
static void notification_sink_task(void *arg)
{
    (void)arg;
    uint8_t buf[NOTIFICATION_SINK_BUF_SIZE];
    size_t len;
    for (;;) {
        /* river2_ble_wait_notification() converts this to ticks via
         * pdMS_TO_TICKS(), so portMAX_DELAY (already a tick count) would
         * overflow instead of meaning "forever" here. Loop on a long finite
         * timeout instead; ESP_ERR_TIMEOUT is harmless in this sink. */
        river2_ble_wait_notification(buf, sizeof(buf), &len, NOTIFICATION_SINK_POLL_TIMEOUT_MS);
    }
}

/* cfg.ef_mac layout: [0:6]=the 6-byte device BLE address, [6:8]=unused
 * padding (the stored NVS blob is only 6 bytes; nvs_get_blob() accepts a
 * larger destination buffer, leaving the rest zeroed). Its address *type*
 * isn't stored, so the scanner matches on address bytes alone and uses the
 * discovered advertisement's own type for the connect call. */

void app_main(void)
{
    esp_err_t r;

    r = nvs_flash_init();
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "NVS Initialization failed with error: %s", esp_err_to_name(r));

    r = read_ecoflow_config(&cfg);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "read_ecoflow_config() failed");

    ESP_LOGI(__func__, "User ID: %s", cfg.ef_user);
    ESP_LOGI(__func__, "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
                cfg.ef_mac[0], cfg.ef_mac[1], cfg.ef_mac[2],
                cfg.ef_mac[3], cfg.ef_mac[4], cfg.ef_mac[5]);

    uint8_t lut_md5[16];
    river2_md5(cfg.lookup_table, sizeof(cfg.lookup_table), lut_md5);
    char md5_str[33]; /* 32 hex chars + null terminator */
    for (int i = 0; i < 16; i++) {
        sprintf(&md5_str[i * 2], "%02x", lut_md5[i]);
    }
    ESP_LOGI(__func__, "Lookup table MD5: %s", md5_str);

    r = river2_ble_init();
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "river2_ble_init() failed");

    char serial[DEVICE_SERIAL_BUF_SIZE];
    uint8_t encrypt_type = 0xFF;
    r = river2_ble_connect(cfg.ef_mac, serial, sizeof(serial), &encrypt_type, BLE_CONNECT_TIMEOUT_MS);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "river2_ble_connect() failed");

    ESP_LOGI(__func__, "Connected to device serial=%s encrypt_type=%u", serial, encrypt_type);
    if (encrypt_type != 7) {
        ESP_LOGE(__func__, "Unsupported encrypt_type %u (only 7 is implemented)", encrypt_type);
        return;
    }

    r = river2_auth_run(&cfg, serial);
    if (r != ESP_OK) {
        ESP_LOGE(__func__, "Authentication failed: %s", esp_err_to_name(r));
        return;
    }

    ESP_LOGI(__func__, "Connection and authentication complete");

    xTaskCreate(notification_sink_task, "notif_sink", NOTIFICATION_SINK_STACK_SIZE, NULL,
                tskIDLE_PRIORITY + 1, NULL);
}
