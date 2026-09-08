#include "river2_commands.h"
#include "river2_packet.h"

esp_err_t river2_set_ac_output(const river2_session_t *session, bool enable)
{
    uint8_t payload[7] = {enable ? 1 : 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return river2_send_command(session, RIVER2_ADDR_MPPT, 0x20, 0x42, payload, sizeof(payload));
}

esp_err_t river2_set_dc_output(const river2_session_t *session, bool enable)
{
    uint8_t payload[1] = {enable ? 1 : 0};
    return river2_send_command(session, RIVER2_ADDR_MPPT, 0x20, 0x51, payload, sizeof(payload));
}
