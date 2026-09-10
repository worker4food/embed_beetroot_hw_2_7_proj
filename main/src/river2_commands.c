#include <string.h>

#include "river2_commands.h"
#include "river2_packet.h"

esp_err_t river2_set_ac_output(const river2_session_t *session, bool enable)
{
    river2_cmd_ac_output_t payload;
    memset(&payload, RIVER2_CMD_FIELD_UNCHANGED, sizeof(payload));
    payload.ac_enable = enable ? 1 : 0;
    return river2_send_command(session, RIVER2_ADDR_MPPT, RIVER2_CMDSET_CONTROL, RIVER2_CMDID_AC_OUTPUT,
                                (const uint8_t *)&payload, sizeof(payload));
}

esp_err_t river2_set_dc_output(const river2_session_t *session, bool enable)
{
    river2_cmd_dc_output_t payload = {.enable = enable ? 1 : 0};
    return river2_send_command(session, RIVER2_ADDR_MPPT, RIVER2_CMDSET_CONTROL, RIVER2_CMDID_DC_OUTPUT,
                                (const uint8_t *)&payload, sizeof(payload));
}
