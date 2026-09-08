
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_check.h>

#include "app_events.h"
#include "ble_client.h"
#include "config.h"
#include "pushbutton.h"
#include "river2_auth.h"
#include "river2_commands.h"
#include "river2_telemetry.h"

ecoflow_config_t cfg;

#define HOST_SYNC_TIMEOUT_MS 10000
#define BLE_CONNECT_TIMEOUT_MS 30000
#define BATTERY_LOG_INTERVAL_MS 60000

typedef enum {
    APP_STATE_WAIT_HOST_SYNC,
    APP_STATE_CONNECTING,
    APP_STATE_STREAMING,
} app_state_t;

static esp_err_t connect_and_authenticate(river2_session_t *out_session)
{
    esp_err_t r;
    app_state_t state = APP_STATE_WAIT_HOST_SYNC;
    while (state != APP_STATE_STREAMING) {
        uint32_t timeout_ms = (state == APP_STATE_WAIT_HOST_SYNC) ? HOST_SYNC_TIMEOUT_MS
                                                                    : BLE_CONNECT_TIMEOUT_MS;
        uint32_t bits = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            ESP_LOGE(__func__, "Timed out in state %d waiting for a BLE event", (int)state);
            return ESP_ERR_TIMEOUT;
        }

        switch (state) {
        case APP_STATE_WAIT_HOST_SYNC:
            if (!(bits & BLE_EVT_HOST_SYNC)) {
                break; /* stray event; loop again */
            }
            r = river2_ble_start_connect(cfg.ef_mac);
            if (r != ESP_OK) {
                ESP_LOGE(__func__, "river2_ble_start_connect() failed: %s", esp_err_to_name(r));
                return r;
            }
            state = APP_STATE_CONNECTING;
            break;

        case APP_STATE_CONNECTING:
            if (bits & BLE_EVT_CONNECT_ERROR) {
                ESP_LOGE(__func__, "Failed to connect to the device");
                return ESP_FAIL;
            }
            if (!(bits & BLE_EVT_CONNECTED)) {
                break; /* stray event; loop again */
            }
            r = river2_auth_run(&cfg, cfg.ef_serial, out_session);
            if (r != ESP_OK) {
                ESP_LOGE(__func__, "Authentication failed: %s", esp_err_to_name(r));
                return r;
            }
            ESP_LOGI(__func__, "Connection and authentication complete");
            state = APP_STATE_STREAMING;
            break;

        case APP_STATE_STREAMING:
            break;
        }
    }
    return ESP_OK;
}

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
    char md5_str[33] = {0}; /* 32 hex chars + null terminator */
    for (int i = 0; i < 16; i++) {
        sprintf(&md5_str[i * 2], "%02x", lut_md5[i]);
    }
    ESP_LOGI(__func__, "Lookup table MD5: %s", md5_str);

    r = gpio_install_isr_service(0);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "gpio_install_isr_service() failed");

    pushbutton_t toggle_ac_btn, toggle_dc_btn;
    r = pushbutton_init(GPIO_NUM_36, TOGGLE_AC_PORT_EVT, &toggle_ac_btn);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "pushbutton_init() failed for pin 36");
    r = pushbutton_init(GPIO_NUM_37, TOGGLE_DC_PORT_EVT, &toggle_dc_btn);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "pushbutton_init() failed for pin 37");

    r = river2_ble_init();
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "river2_ble_init() failed");

    river2_session_t session;
    r = connect_and_authenticate(&session);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "connect_and_authenticate() failed");

    r = river2_telemetry_start(&session, xTaskGetCurrentTaskHandle(), BATTERY_LOG_INTERVAL_MS);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "river2_telemetry_start() failed");

    bool ac_on = false;
    bool dc_on = false;
    bool ac_state_known = false;
    bool dc_state_known = false;

    for (;;) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
        if (bits & RIVER2_TELEMETRY_EVT_AC_STATE) {
            ac_on = river2_telemetry_ac_enabled() != 0;
            ac_state_known = true;
            ESP_LOGI(__func__, "AC port state known: %s", ac_on ? "on" : "off");
        }
        if (bits & RIVER2_TELEMETRY_EVT_DC_STATE) {
            dc_on = river2_telemetry_dc_out_state() != 0;
            dc_state_known = true;
            ESP_LOGI(__func__, "DC port state known: %s", dc_on ? "on" : "off");
        }
        if (bits & RIVER2_TELEMETRY_EVT_BATTERY_LEVEL) {
            ESP_LOGI(__func__, "Battery level: %d%%", river2_telemetry_battery_percent());
        }
        if (bits & TOGGLE_AC_PORT_EVT) {
            if (!ac_state_known) {
                ESP_LOGW(__func__, "Ignoring AC toggle: AC port state not yet known");
            } else {
                bool want_on = !ac_on;
                r = river2_set_ac_output(&session, want_on);
                if (r != ESP_OK) {
                    ESP_LOGE(__func__, "Failed to toggle AC port: %s", esp_err_to_name(r));
                } else {
                    ac_on = want_on;
                    ESP_LOGI(__func__, "Toggled AC port %s", ac_on ? "on" : "off");
                }
            }
        }
        if (bits & TOGGLE_DC_PORT_EVT) {
            if (!dc_state_known) {
                ESP_LOGW(__func__, "Ignoring DC toggle: DC port state not yet known");
            } else {
                bool want_on = !dc_on;
                r = river2_set_dc_output(&session, want_on);
                if (r != ESP_OK) {
                    ESP_LOGE(__func__, "Failed to toggle DC port: %s", esp_err_to_name(r));
                } else {
                    dc_on = want_on;
                    ESP_LOGI(__func__, "Toggled DC port %s", dc_on ? "on" : "off");
                }
            }
        }
    }
}
