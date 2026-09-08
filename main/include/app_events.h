#pragma once

/* All task-notification bits, kept in one place so allocation can't collide. */

/* ble_client.h: BLE lifecycle events. */
#define BLE_EVT_HOST_SYNC     (1u << 0)
#define BLE_EVT_CONNECTED     (1u << 1)
#define BLE_EVT_CONNECT_ERROR (1u << 2)
#define BLE_EVT_DISCONNECTED  (1u << 3)

/* river2_telemetry.h: telemetry-derived state events. */
#define RIVER2_TELEMETRY_EVT_BATTERY_LEVEL (1u << 4)
#define RIVER2_TELEMETRY_EVT_DC_STATE      (1u << 7)
#define RIVER2_TELEMETRY_EVT_AC_STATE      (1u << 8)

/* app_main.c: pushbutton events. */
#define TOGGLE_AC_PORT_EVT (1u << 5)
#define TOGGLE_DC_PORT_EVT (1u << 6)
