#pragma once

#include <stdbool.h>
#include <esp_err.h>

#include "river2_auth.h"

/* Enables/disables AC output (protocol doc §6, cmd_id=0x42). Leaves AC
 * X-Boost untouched (0xFF filler bytes). */
esp_err_t river2_set_ac_output(const river2_session_t *session, bool enable);

/* Enables/disables the 12V DC output (protocol doc §6, cmd_id=0x51). */
esp_err_t river2_set_dc_output(const river2_session_t *session, bool enable);
