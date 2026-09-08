#include <stdbool.h>
#include <string.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>

#include "ble_client.h"

static const char *TAG = "ble_client";

#define MAX_SERVICES 24
#define HOST_SYNC_TIMEOUT_MS 10000
#define GAP_CONNECT_TIMEOUT_MS 30000

extern void ble_store_config_init(void);

/* GATT characteristic UUIDs to try (protocol doc §2). The "rfcomm-style"
 * pair's 128-bit form (00000002/00000003-0000-1000-8000-00805f9b34fb) is the
 * Bluetooth SIG Base UUID expansion of 16-bit UUIDs 0x0002/0x0003. Real
 * devices have been observed sending them as native 16-bit UUIDs on the wire,
 * which ble_uuid_cmp() never treats as equal to their 128-bit expansion (it
 * only compares same-width UUIDs), so both widths are matched here. */
static const uint8_t RFCOMM_WRITE_UUID128[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
};
static const uint8_t RFCOMM_NOTIFY_UUID128[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
};
#define RFCOMM_WRITE_UUID16 0x0002
#define RFCOMM_NOTIFY_UUID16 0x0003
static const uint8_t NUS_WRITE_UUID[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e,
};
static const uint8_t NUS_NOTIFY_UUID[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e,
};

typedef enum {
    BLE_EVENT_HOST_SYNC,
    BLE_EVENT_CONNECT_DONE,
    BLE_EVENT_NOTIFY,
} ble_event_type_t;

typedef struct {
    ble_event_type_t type;
    esp_err_t connect_result; /* BLE_EVENT_CONNECT_DONE */
    uint8_t *notify_data;     /* BLE_EVENT_NOTIFY */
    size_t notify_len;        /* BLE_EVENT_NOTIFY */
} ble_event_t;

static QueueHandle_t s_ble_queue;

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_write_handle;
static uint16_t s_notify_handle;
static uint16_t s_notify_svc_end;
static bool s_cccd_found;

static struct {
    uint16_t start;
    uint16_t end;
} s_services[MAX_SERVICES];
static int s_service_count;
static int s_service_idx;

/* forward declarations: the discovery chain calls these out of file order
 * (each begin_*_discovery() kicks off the next stage's callback). */
static int disc_dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg);
static int disc_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr, void *arg);
static int disc_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        const struct ble_gatt_svc *service, void *arg);

static void finish_connect(esp_err_t result)
{
    ble_event_t evt = {.type = BLE_EVENT_CONNECT_DONE, .connect_result = result};
    xQueueSend(s_ble_queue, &evt, 0);
}

static bool uuid_matches(const ble_uuid_t *u, const uint8_t bytes[16])
{
    ble_uuid128_t candidate;
    candidate.u.type = BLE_UUID_TYPE_128;
    memcpy(candidate.value, bytes, 16);
    return ble_uuid_cmp(u, &candidate.u) == 0;
}

static bool uuid_matches16(const ble_uuid_t *u, uint16_t value)
{
    ble_uuid16_t candidate = {.u = {.type = BLE_UUID_TYPE_16}, .value = value};
    return ble_uuid_cmp(u, &candidate.u) == 0;
}

static void begin_descriptor_discovery(void)
{
    int rc = ble_gattc_disc_all_dscs(s_conn_handle, s_notify_handle, s_notify_svc_end,
                                      disc_dsc_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start descriptor discovery; rc=%d", rc);
        finish_connect(ESP_FAIL);
    }
}

static int on_cccd_write(uint16_t conn_handle, const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;
    if (error->status != 0) {
        ESP_LOGE(TAG, "Failed to write CCCD; status=%d", error->status);
        finish_connect(ESP_FAIL);
        return 0;
    }
    ESP_LOGI(TAG, "Subscribed to notifications");
    finish_connect(ESP_OK);
    return 0;
}

static int disc_dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)chr_val_handle;
    (void)arg;

    if (error->status == 0) {
        if (!s_cccd_found &&
            ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            s_cccd_found = true;
            uint8_t val[2] = {0x01, 0x00};
            int rc = ble_gattc_write_flat(conn_handle, dsc->handle, val, sizeof(val),
                                           on_cccd_write, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "Failed to write CCCD; rc=%d", rc);
                finish_connect(ESP_FAIL);
            }
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (!s_cccd_found) {
            ESP_LOGE(TAG, "Notify characteristic has no CCCD descriptor");
            finish_connect(ESP_ERR_NOT_FOUND);
        }
        return 0;
    }

    ESP_LOGE(TAG, "Descriptor discovery failed; status=%d", error->status);
    finish_connect(ESP_FAIL);
    return 0;
}

static void begin_chr_discovery_for_service(int idx)
{
    int rc = ble_gattc_disc_all_chrs(s_conn_handle, s_services[idx].start, s_services[idx].end,
                                      disc_chr_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start characteristic discovery; rc=%d", rc);
        finish_connect(ESP_FAIL);
    }
}

static int disc_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn_handle;
    (void)arg;

    if (error->status == 0) {
        char uuid_str[BLE_UUID_STR_LEN];
        ESP_LOGI(TAG, "  chr uuid=%s val_handle=%u properties=0x%02x",
                 ble_uuid_to_str(&chr->uuid.u, uuid_str), chr->val_handle, chr->properties);

        if (s_write_handle == 0 &&
            (uuid_matches(&chr->uuid.u, RFCOMM_WRITE_UUID128) ||
             uuid_matches16(&chr->uuid.u, RFCOMM_WRITE_UUID16) ||
             uuid_matches(&chr->uuid.u, NUS_WRITE_UUID))) {
            s_write_handle = chr->val_handle;
            ESP_LOGI(TAG, "Found write characteristic, handle=%u", s_write_handle);
        }
        if (s_notify_handle == 0 &&
            (uuid_matches(&chr->uuid.u, RFCOMM_NOTIFY_UUID128) ||
             uuid_matches16(&chr->uuid.u, RFCOMM_NOTIFY_UUID16) ||
             uuid_matches(&chr->uuid.u, NUS_NOTIFY_UUID))) {
            s_notify_handle = chr->val_handle;
            s_notify_svc_end = s_services[s_service_idx].end;
            ESP_LOGI(TAG, "Found notify characteristic, handle=%u", s_notify_handle);
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        s_service_idx++;
        if (s_service_idx < s_service_count) {
            begin_chr_discovery_for_service(s_service_idx);
        } else if (s_write_handle == 0 || s_notify_handle == 0) {
            ESP_LOGE(TAG, "Device does not expose a supported characteristic pair");
            finish_connect(ESP_ERR_NOT_SUPPORTED);
        } else {
            begin_descriptor_discovery();
        }
        return 0;
    }

    ESP_LOGE(TAG, "Characteristic discovery failed; status=%d", error->status);
    finish_connect(ESP_FAIL);
    return 0;
}

static int disc_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        const struct ble_gatt_svc *service, void *arg)
{
    (void)conn_handle;
    (void)arg;

    if (error->status == 0) {
        char uuid_str[BLE_UUID_STR_LEN];
        ESP_LOGI(TAG, "svc uuid=%s start=%u end=%u", ble_uuid_to_str(&service->uuid.u, uuid_str),
                 service->start_handle, service->end_handle);

        if (s_service_count < MAX_SERVICES) {
            s_services[s_service_count].start = service->start_handle;
            s_services[s_service_count].end = service->end_handle;
            s_service_count++;
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_service_count == 0) {
            ESP_LOGE(TAG, "Device exposes no GATT services");
            finish_connect(ESP_ERR_NOT_FOUND);
            return 0;
        }
        s_service_idx = 0;
        begin_chr_discovery_for_service(0);
        return 0;
    }

    ESP_LOGE(TAG, "Service discovery failed; status=%d", error->status);
    finish_connect(ESP_FAIL);
    return 0;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_service_count = 0;
            s_service_idx = 0;
            s_write_handle = 0;
            s_notify_handle = 0;
            s_cccd_found = false;
            ESP_LOGI(TAG, "Connected, discovering services");
            int rc = ble_gattc_disc_all_svcs(s_conn_handle, disc_svc_cb, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "Failed to start service discovery; rc=%d", rc);
                finish_connect(ESP_FAIL);
            }
        } else {
            ESP_LOGE(TAG, "Connection failed; status=%d", event->connect.status);
            finish_connect(ESP_FAIL);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        int len = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (len <= 0) {
            return 0;
        }
        uint8_t *buf = malloc((size_t)len);
        if (buf == NULL) {
            return 0;
        }
        uint16_t out_len = 0;
        if (ble_hs_mbuf_to_flat(event->notify_rx.om, buf, (uint16_t)len, &out_len) != 0) {
            free(buf);
            return 0;
        }
        ble_event_t evt = {.type = BLE_EVENT_NOTIFY, .notify_data = buf, .notify_len = out_len};
        if (xQueueSend(s_ble_queue, &evt, 0) != pdTRUE) {
            ESP_LOGW(TAG, "BLE event queue full, dropping %u bytes", out_len);
            free(buf);
        }
        return 0;
    }

    default:
        return 0;
    }
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset; reason=%d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed; rc=%d", rc);
    }
    ble_event_t evt = {.type = BLE_EVENT_HOST_SYNC};
    xQueueSend(s_ble_queue, &evt, 0);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t river2_ble_init(void)
{
    s_ble_queue = xQueueCreate(16, sizeof(ble_event_t));
    if (s_ble_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NimBLE port: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_device_name_set("river2pro-bridge");
    ble_store_config_init();

    nimble_port_freertos_init(host_task);

    ble_event_t evt;
    if (xQueueReceive(s_ble_queue, &evt, pdMS_TO_TICKS(HOST_SYNC_TIMEOUT_MS)) != pdTRUE ||
        evt.type != BLE_EVENT_HOST_SYNC) {
        ESP_LOGE(TAG, "Timed out waiting for NimBLE host sync");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t river2_ble_connect(const uint8_t target_addr[6], uint32_t timeout_ms)
{
    /* `target_addr` arrives in conventional display order (as produced by
     * NVS's hex2bin from a string like "64E833C97C29"), but NimBLE's
     * ble_addr_t.val[] stores addresses reversed relative to that (val[0] is
     * the last displayed octet, see e.g. nimble_central_utils' addr_str(),
     * which prints val[5..0]). Reverse here so ble_gap_connect() gets the
     * address in NimBLE's order. */
    ble_addr_t peer_addr = {.type = BLE_ADDR_PUBLIC};
    for (int i = 0; i < 6; i++) {
        peer_addr.val[i] = target_addr[5 - i];
    }

    xQueueReset(s_ble_queue); /* drop any stale events from a previous connection attempt */

    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "error determining address type; rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gap_connect(own_addr_type, &peer_addr, GAP_CONNECT_TIMEOUT_MS, NULL, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initiate connection; rc=%d", rc);
        return ESP_FAIL;
    }

    ble_event_t evt;
    if (xQueueReceive(s_ble_queue, &evt, pdMS_TO_TICKS(timeout_ms)) != pdTRUE ||
        evt.type != BLE_EVENT_CONNECT_DONE) {
        ble_gap_conn_cancel();
        ESP_LOGE(TAG, "Timed out connecting to the device");
        return ESP_ERR_TIMEOUT;
    }
    return evt.connect_result;
}

esp_err_t river2_ble_write(const uint8_t *data, size_t len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_write_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    int rc = ble_gattc_write_flat(s_conn_handle, s_write_handle, data, len, NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT write failed; rc=%d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t river2_ble_wait_notification(uint8_t *out_buf, size_t out_cap, size_t *out_len,
                                        uint32_t timeout_ms)
{
    ble_event_t evt;
    if (xQueueReceive(s_ble_queue, &evt, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (evt.type != BLE_EVENT_NOTIFY) {
        ESP_LOGE(TAG, "Unexpected BLE event type %d while waiting for a notification", evt.type);
        return ESP_ERR_INVALID_STATE;
    }
    size_t copy_len = evt.notify_len < out_cap ? evt.notify_len : out_cap;
    memcpy(out_buf, evt.notify_data, copy_len);
    *out_len = copy_len;
    free(evt.notify_data);
    return ESP_OK;
}
