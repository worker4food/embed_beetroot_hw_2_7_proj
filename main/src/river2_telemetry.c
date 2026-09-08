#include <stdlib.h>

#include <esp_timer.h>

#include "ble_client.h"
#include "river2_packet.h"
#include "river2_telemetry.h"

#define TELEMETRY_STACK_SIZE 4096
#define NOTIFY_CHUNK_MAX 512
#define POLL_TIMEOUT_MS 5000

/* PD heartbeat field 6 `soc` (protocol doc §5.1), offset by the preceding
 * fields' widths: model(B,1) + err_code(4s,4) + sys_ver(4s,4) +
 * wifi_ver(4s,4) + wifi_auto_rcvy(B,1). */
#define PD_SOC_OFFSET 14

typedef struct {
    river2_session_t session;
    TaskHandle_t owner_task;
    uint32_t interval_ms;
} telemetry_ctx_t;

static volatile int8_t s_battery_percent = -1;

static void telemetry_task(void *arg)
{
    telemetry_ctx_t *ctx = arg;

    river2_framebuf_t fb;
    river2_framebuf_init(&fb);

    int64_t next_notify_us = 0;

    for (;;) {
        uint8_t chunk[NOTIFY_CHUNK_MAX];
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
            if (pkt.src != RIVER2_ADDR_PD || pkt.cmd_set != RIVER2_CMDSET_PD_HEARTBEAT ||
                pkt.cmd_id != RIVER2_CMDID_PD_HEARTBEAT || pkt.payload_len <= PD_SOC_OFFSET) {
                continue;
            }

            s_battery_percent = (int8_t)pkt.payload[PD_SOC_OFFSET];

            int64_t now_us = esp_timer_get_time();
            if (now_us >= next_notify_us) {
                xTaskNotify(ctx->owner_task, RIVER2_TELEMETRY_EVT_BATTERY_LEVEL, eSetBits);
                next_notify_us = now_us + (int64_t)ctx->interval_ms * 1000;
            }
        }
    }
}

esp_err_t river2_telemetry_start(const river2_session_t *session, TaskHandle_t owner_task,
                                  uint32_t interval_ms)
{
    telemetry_ctx_t *ctx = malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ctx->session = *session;
    ctx->owner_task = owner_task;
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
