#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_err.h>

#include "app_events.h"
#include "river2_auth.h"

typedef enum {
    RIVER2_PORT_UNKNOWN = -1,
    RIVER2_PORT_OFF = 0,
    RIVER2_PORT_ON = 1,
} river2_port_state_t;

/* Starts a background task that reads BLE notifications, decrypts and
 * decodes PD (protocol doc §5.1) and MPPT (§5.5) heartbeats under `session`,
 * and sets the RIVER2_TELEMETRY_EVT_* bits in `events` on battery/DC/AC
 * state changes. Call after river2_auth_run() succeeds.
 * Returns once the task is created; the task itself runs forever. */
esp_err_t river2_telemetry_start(const river2_session_t *session, EventGroupHandle_t events,
                                  uint32_t interval_ms);

/* Most recently decoded battery percentage (0-100), or -1 if none has
 * arrived yet. Safe to call after RIVER2_TELEMETRY_EVT_BATTERY_LEVEL. */
int river2_telemetry_battery_percent(void);

/* Most recently decoded 12V DC output state. Safe to call after
 * RIVER2_TELEMETRY_EVT_DC_STATE. */
river2_port_state_t river2_telemetry_dc_out_state(void);

/* Most recently decoded AC output state. Safe to call after
 * RIVER2_TELEMETRY_EVT_AC_STATE. */
river2_port_state_t river2_telemetry_ac_enabled(void);
