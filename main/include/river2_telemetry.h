#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>

#include "app_events.h" /* RIVER2_TELEMETRY_EVT_BATTERY_LEVEL */
#include "river2_auth.h"

/* Starts a background task that reads BLE notifications, decrypts and
 * decodes PD (protocol doc §5.1) and MPPT (§5.5) heartbeats under `session`,
 * and notifies `owner_task` of battery/DC/AC state changes (see the
 * RIVER2_TELEMETRY_EVT_* bits below). Call after river2_auth_run() succeeds.
 * Returns once the task is created; the task itself runs forever. */
esp_err_t river2_telemetry_start(const river2_session_t *session, TaskHandle_t owner_task,
                                  uint32_t interval_ms);

/* Most recently decoded battery percentage (0-100), or -1 if none has
 * arrived yet. Safe to call from `owner_task` after it observes
 * RIVER2_TELEMETRY_EVT_BATTERY_LEVEL. */
int river2_telemetry_battery_percent(void);

/* Most recently decoded 12V DC output state (0=off, nonzero=on), or -1 if
 * none has arrived yet. Safe to call after RIVER2_TELEMETRY_EVT_DC_STATE. */
int river2_telemetry_dc_out_state(void);

/* Most recently decoded AC output state (0=off, nonzero=on), or -1 if none
 * has arrived yet. Safe to call after RIVER2_TELEMETRY_EVT_AC_STATE. */
int river2_telemetry_ac_enabled(void);
