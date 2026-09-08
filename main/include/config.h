#pragma once

#include <stdint.h>
#include <esp_err.h>

#define USER_ID_SIZE 20
#define MAC_SIZE     6
#define LUT_SIZE     65280

typedef struct {
    char ef_user[USER_ID_SIZE];
    uint8_t ef_mac[MAC_SIZE];
    uint8_t lookup_table[LUT_SIZE];
} ecoflow_config_t;

esp_err_t read_ecoflow_config(ecoflow_config_t *config);
