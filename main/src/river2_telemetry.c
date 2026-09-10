#include <stdlib.h>

#include <esp_timer.h>

#include "ble_client.h"
#include "river2_heartbeat.h"
#include "river2_packet.h"
#include "river2_telemetry.h"

#define TELEMETRY_STACK_SIZE 4096
#define POLL_TIMEOUT_MS 5000

typedef struct {
    river2_session_t session;
    EventGroupHandle_t events;
    uint32_t interval_ms;
} telemetry_ctx_t;

static volatile int16_t s_battery_percent = -1;
static volatile river2_port_state_t s_dc_out_state = RIVER2_PORT_UNKNOWN;
static volatile river2_port_state_t s_ac_enabled = RIVER2_PORT_UNKNOWN;

static void telemetry_task(void *arg)
{
    telemetry_ctx_t *ctx = arg;

    river2_framebuf_t fb;
    river2_framebuf_init(&fb);

    int64_t next_notify_us = 0;

    for (;;) {
        uint8_t chunk[RIVER2_BLE_NOTIFY_CHUNK_MAX];
        size_t chunk_len = 0;
        esp_err_t err = river2_ble_wait_notification(chunk, sizeof(chunk), &chunk_len, POLL_TIMEOUT_MS);
        if (err != ESP_OK || !river2_framebuf_append(&fb, chunk, chunk_len)) {
            continue;
        }

        uint8_t frame_type;
        const uint8_t *payload;
        size_t payload_len;
        while (river2_framebuf_extract(&fb, &frame_type, &payload, &payload_len)) {
            if (frame_type != RIVER2_FRAME_DATA) {
                continue;
            }

            uint8_t decrypted[256];
            size_t decrypted_len = 0;
            if (river2_session_decrypt(&ctx->session, payload, payload_len, decrypted, sizeof(decrypted),
                                        &decrypted_len) != ESP_OK) {
                continue;
            }

            river2_inner_packet_t pkt;
            if (!river2_inner_parse(decrypted, decrypted_len, &pkt)) {
                continue;
            }
            if (pkt.cmd_id != RIVER2_CMDID_HEARTBEAT) {
                continue;
            }

            /* Only the battery-level notification is throttled by interval_ms;
             * DC/AC state notifications are edge-triggered. */
            uint32_t notify_bits = 0;
            if (pkt.src == RIVER2_ADDR_PD && pkt.cmd_set == RIVER2_CMDSET_HEARTBEAT) {
                const river2_pd_heartbeat_t *pd = (const river2_pd_heartbeat_t *)pkt.payload;
                if (RIVER2_HEARTBEAT_FIELD_PRESENT(pkt.payload_len, river2_pd_heartbeat_t, soc)) {
                    s_battery_percent = pd->soc;
                    int64_t now_us = esp_timer_get_time();
                    if (now_us >= next_notify_us) {
                        notify_bits |= RIVER2_TELEMETRY_EVT_BATTERY_LEVEL;
                        next_notify_us = now_us + (int64_t)ctx->interval_ms * 1000;
                    }
                }
            } else if (pkt.src == RIVER2_ADDR_MPPT && pkt.cmd_set == RIVER2_CMDSET_HEARTBEAT) {
                const river2_mppt_heartbeat_t *mppt = (const river2_mppt_heartbeat_t *)pkt.payload;
                if (RIVER2_HEARTBEAT_FIELD_PRESENT(pkt.payload_len, river2_mppt_heartbeat_t, cfg_ac_enabled)) {
                    river2_port_state_t ac_enabled = mppt->cfg_ac_enabled ? RIVER2_PORT_ON : RIVER2_PORT_OFF;
                    if (ac_enabled != s_ac_enabled) {
                        s_ac_enabled = ac_enabled;
                        notify_bits |= RIVER2_TELEMETRY_EVT_AC_STATE;
                    }
                }
                if (RIVER2_HEARTBEAT_FIELD_PRESENT(pkt.payload_len, river2_mppt_heartbeat_t, car_state)) {
                    river2_port_state_t dc_out_state = mppt->car_state ? RIVER2_PORT_ON : RIVER2_PORT_OFF;
                    if (dc_out_state != s_dc_out_state) {
                        s_dc_out_state = dc_out_state;
                        notify_bits |= RIVER2_TELEMETRY_EVT_DC_STATE;
                    }
                }
            }

            if (notify_bits != 0) {
                xEventGroupSetBits(ctx->events, notify_bits);
            }
        }
    }
}

esp_err_t river2_telemetry_start(const river2_session_t *session, EventGroupHandle_t events,
                                  uint32_t interval_ms)
{
    telemetry_ctx_t *ctx = malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ctx->session = *session;
    ctx->events = events;
    ctx->interval_ms = interval_ms;

    if (xTaskCreate(telemetry_task, "river2_telem", TELEMETRY_STACK_SIZE, ctx, tskIDLE_PRIORITY + 1,
                     NULL) != pdPASS) {
        free(ctx);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

int river2_telemetry_battery_percent(void)
{
    return s_battery_percent;
}

river2_port_state_t river2_telemetry_dc_out_state(void)
{
    return s_dc_out_state;
}

river2_port_state_t river2_telemetry_ac_enabled(void)
{
    return s_ac_enabled;
}
