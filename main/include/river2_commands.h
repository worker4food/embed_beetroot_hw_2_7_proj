#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

#include "river2_auth.h"

#define RIVER2_CMD_FIELD_UNCHANGED 0xFF

#define RIVER2_CMD_AC_OUTPUT_SIZE 7

typedef struct {
    uint8_t ac_enable;
    uint8_t ac_xboost;
    uint8_t reserved[5];
} __attribute__((packed)) river2_cmd_ac_output_t;

_Static_assert(sizeof(river2_cmd_ac_output_t) == RIVER2_CMD_AC_OUTPUT_SIZE, "river2_cmd_ac_output_t layout mismatch");

#define RIVER2_CMD_DC_OUTPUT_SIZE 1

typedef struct {
    uint8_t enable;
} __attribute__((packed)) river2_cmd_dc_output_t;

_Static_assert(sizeof(river2_cmd_dc_output_t) == RIVER2_CMD_DC_OUTPUT_SIZE, "river2_cmd_dc_output_t layout mismatch");

#define RIVER2_CMD_AC_ALWAYS_ON_SIZE 2

typedef struct {
    uint8_t enable;
    uint8_t fixed_0x05;
} __attribute__((packed)) river2_cmd_ac_always_on_t;

_Static_assert(sizeof(river2_cmd_ac_always_on_t) == RIVER2_CMD_AC_ALWAYS_ON_SIZE,
               "river2_cmd_ac_always_on_t layout mismatch");

#define RIVER2_CMD_ENERGY_BACKUP_SIZE 4

typedef struct {
    uint8_t enable;
    uint8_t percent;
    uint8_t reserved[2];
} __attribute__((packed)) river2_cmd_energy_backup_t;

_Static_assert(sizeof(river2_cmd_energy_backup_t) == RIVER2_CMD_ENERGY_BACKUP_SIZE,
               "river2_cmd_energy_backup_t layout mismatch");

#define RIVER2_CMD_BATTERY_LIMIT_SIZE 1

typedef struct {
    uint8_t percent;
} __attribute__((packed)) river2_cmd_battery_limit_t;

_Static_assert(sizeof(river2_cmd_battery_limit_t) == RIVER2_CMD_BATTERY_LIMIT_SIZE,
               "river2_cmd_battery_limit_t layout mismatch");

#define RIVER2_CMD_DC_CHARGE_CURRENT_SIZE 4

typedef struct {
    uint32_t milliamps;
} __attribute__((packed)) river2_cmd_dc_charge_current_t;

_Static_assert(sizeof(river2_cmd_dc_charge_current_t) == RIVER2_CMD_DC_CHARGE_CURRENT_SIZE,
               "river2_cmd_dc_charge_current_t layout mismatch");

#define RIVER2_CMD_DC_CHARGE_MODE_SIZE 1

typedef struct {
    uint8_t chg_type;
} __attribute__((packed)) river2_cmd_dc_charge_mode_t;

_Static_assert(sizeof(river2_cmd_dc_charge_mode_t) == RIVER2_CMD_DC_CHARGE_MODE_SIZE,
               "river2_cmd_dc_charge_mode_t layout mismatch");

#define RIVER2_CMD_AC_CHARGE_SPEED_SIZE 3

typedef struct {
    uint16_t watts;
    uint8_t reserved;
} __attribute__((packed)) river2_cmd_ac_charge_speed_t;

_Static_assert(sizeof(river2_cmd_ac_charge_speed_t) == RIVER2_CMD_AC_CHARGE_SPEED_SIZE,
               "river2_cmd_ac_charge_speed_t layout mismatch");

/* Enables/disables AC output (protocol doc §6, cmd_id=0x42). Leaves AC
 * X-Boost untouched (0xFF filler bytes). */
esp_err_t river2_set_ac_output(const river2_session_t *session, bool enable);

/* Enables/disables the 12V DC output (protocol doc §6, cmd_id=0x51). */
esp_err_t river2_set_dc_output(const river2_session_t *session, bool enable);
