#include <nvs.h>
#include <esp_check.h>

#include "config.h"

esp_err_t read_ecoflow_config(ecoflow_config_t *config)
{
    nvs_handle_t nvs_h;

    esp_err_t err = nvs_open("ecoflow", NVS_READONLY, &nvs_h);
    ESP_RETURN_ON_ERROR(err, __func__, "Error opening NVS handle");

    size_t size_user = sizeof(config->ef_user);
    err = nvs_get_str(nvs_h, "ef_user", config->ef_user, &size_user);
    ESP_RETURN_ON_ERROR(err, __func__, "Failed to read ef_user %d - %s", err, esp_err_to_name(err));

    size_t size_mac = sizeof(config->ef_mac);
    err = nvs_get_blob(nvs_h, "ef_mac", config->ef_mac, &size_mac);
    ESP_RETURN_ON_ERROR(err, __func__, "Failed to read ef_mac %d - %s", err, esp_err_to_name(err));

    size_t size_serial = sizeof(config->ef_serial);
    err = nvs_get_str(nvs_h, "ef_serial", config->ef_serial, &size_serial);
    ESP_RETURN_ON_ERROR(err, __func__, "Failed to read ef_serial %d - %s", err, esp_err_to_name(err));

    size_t size_lut = sizeof(config->lookup_table);
    err = nvs_get_blob(nvs_h, "lookup_table", config->lookup_table, &size_lut);
    ESP_RETURN_ON_ERROR(err, __func__, "Failed to read lookup_table %d - %s", err, esp_err_to_name(err));

    nvs_close(nvs_h);
    return ESP_OK;
}
