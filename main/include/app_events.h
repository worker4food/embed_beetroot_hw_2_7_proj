#pragma once

#define BLE_EVT_HOST_SYNC     (1u << 0)
#define BLE_EVT_CONNECTED     (1u << 1)
#define BLE_EVT_CONNECT_ERROR (1u << 2)
#define BLE_EVT_DISCONNECTED  (1u << 3)

#define RIVER2_TELEMETRY_EVT_BATTERY_LEVEL (1u << 4)
#define RIVER2_TELEMETRY_EVT_DC_STATE      (1u << 5)
#define RIVER2_TELEMETRY_EVT_AC_STATE      (1u << 6)

#define TOGGLE_AC_PORT_EVT (1u << 7)
#define TOGGLE_DC_PORT_EVT (1u << 8)
