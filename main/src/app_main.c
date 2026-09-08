
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
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

#define MAIN_LOOP_EVENTS                                                                       \
    (BLE_EVT_DISCONNECTED | RIVER2_TELEMETRY_EVT_BATTERY_LEVEL | RIVER2_TELEMETRY_EVT_AC_STATE \
     | RIVER2_TELEMETRY_EVT_DC_STATE | TOGGLE_AC_PORT_EVT | TOGGLE_DC_PORT_EVT)

static esp_err_t connect_and_authenticate(EventGroupHandle_t events, river2_session_t *out_session)
{
    EventBits_t bits = xEventGroupWaitBits(events, BLE_EVT_HOST_SYNC, pdTRUE, pdFALSE,
                                            pdMS_TO_TICKS(HOST_SYNC_TIMEOUT_MS));
    if (!(bits & BLE_EVT_HOST_SYNC)) {
        ESP_LOGE(__func__, "Timed out waiting for the BLE host to sync");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t r = river2_ble_start_connect(cfg.ef_mac);
    if (r != ESP_OK) {
        ESP_LOGE(__func__, "river2_ble_start_connect() failed: %s", esp_err_to_name(r));
        return r;
    }

    bits = xEventGroupWaitBits(events,
                                BLE_EVT_CONNECTED | BLE_EVT_CONNECT_ERROR | BLE_EVT_DISCONNECTED,
                                pdTRUE, pdFALSE, pdMS_TO_TICKS(BLE_CONNECT_TIMEOUT_MS));
    if (bits & (BLE_EVT_CONNECT_ERROR | BLE_EVT_DISCONNECTED)) {
        ESP_LOGE(__func__, "Failed to connect to the device");
        return ESP_FAIL;
    }
    if (!(bits & BLE_EVT_CONNECTED)) {
        ESP_LOGE(__func__, "Timed out waiting for the connection to complete");
        return ESP_ERR_TIMEOUT;
    }

    r = river2_auth_run(&cfg, cfg.ef_serial, out_session);
    if (r != ESP_OK) {
        ESP_LOGE(__func__, "Authentication failed: %s", esp_err_to_name(r));
        return r;
    }
    ESP_LOGI(__func__, "Connection and authentication complete");
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

    EventGroupHandle_t events = xEventGroupCreate();
    if (events == NULL) {
        ESP_LOGE(__func__, "xEventGroupCreate() failed");
        return;
    }

    r = gpio_install_isr_service(0);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "gpio_install_isr_service() failed");

    pushbutton_t toggle_ac_btn, toggle_dc_btn;
    r = pushbutton_init(GPIO_NUM_36, TOGGLE_AC_PORT_EVT, events, &toggle_ac_btn);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "pushbutton_init() failed for pin 36");
    r = pushbutton_init(GPIO_NUM_37, TOGGLE_DC_PORT_EVT, events, &toggle_dc_btn);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "pushbutton_init() failed for pin 37");

    r = river2_ble_init(events);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "river2_ble_init() failed");

    river2_session_t session;
    r = connect_and_authenticate(events, &session);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "connect_and_authenticate() failed");

    xEventGroupClearBits(events, TOGGLE_AC_PORT_EVT | TOGGLE_DC_PORT_EVT);

    r = river2_telemetry_start(&session, events, BATTERY_LOG_INTERVAL_MS);
    ESP_RETURN_VOID_ON_ERROR(r, __func__, "river2_telemetry_start() failed");

    for (;;) {
        EventBits_t bits =
            xEventGroupWaitBits(events, MAIN_LOOP_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & BLE_EVT_DISCONNECTED) {
            ESP_LOGE(__func__, "Disconnected from the device");
            return;
        }
        if (bits & RIVER2_TELEMETRY_EVT_AC_STATE) {
            ESP_LOGI(__func__, "AC port state known: %s",
                        river2_telemetry_ac_enabled() == RIVER2_PORT_ON ? "on" : "off");
        }
        if (bits & RIVER2_TELEMETRY_EVT_DC_STATE) {
            ESP_LOGI(__func__, "DC port state known: %s",
                        river2_telemetry_dc_out_state() == RIVER2_PORT_ON ? "on" : "off");
        }
        if (bits & RIVER2_TELEMETRY_EVT_BATTERY_LEVEL) {
            ESP_LOGI(__func__, "Battery level: %d%%", river2_telemetry_battery_percent());
        }
        if (bits & TOGGLE_AC_PORT_EVT) {
            river2_port_state_t ac_state = river2_telemetry_ac_enabled();
            if (ac_state == RIVER2_PORT_UNKNOWN) {
                ESP_LOGW(__func__, "Ignoring AC toggle: AC port state not yet known");
            } else {
                bool want_on = ac_state == RIVER2_PORT_OFF;
                r = river2_set_ac_output(&session, want_on);
                if (r != ESP_OK) {
                    ESP_LOGE(__func__, "Failed to toggle AC port: %s", esp_err_to_name(r));
                } else {
                    ESP_LOGI(__func__, "Requested AC port %s", want_on ? "on" : "off");
                }
            }
        }
        if (bits & TOGGLE_DC_PORT_EVT) {
            river2_port_state_t dc_state = river2_telemetry_dc_out_state();
            if (dc_state == RIVER2_PORT_UNKNOWN) {
                ESP_LOGW(__func__, "Ignoring DC toggle: DC port state not yet known");
            } else {
                bool want_on = dc_state == RIVER2_PORT_OFF;
                r = river2_set_dc_output(&session, want_on);
                if (r != ESP_OK) {
                    ESP_LOGE(__func__, "Failed to toggle DC port: %s", esp_err_to_name(r));
                } else {
                    ESP_LOGI(__func__, "Requested DC port %s", want_on ? "on" : "off");
                }
            }
        }
    }
}
