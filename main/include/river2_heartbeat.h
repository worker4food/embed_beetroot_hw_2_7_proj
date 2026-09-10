#pragma once

#include <stddef.h>
#include <stdint.h>

/* Payload may be shorter than the struct; decode only the leading bytes present. */
#define RIVER2_HEARTBEAT_FIELD_PRESENT(payload_len, type, field) \
    ((payload_len) >= offsetof(type, field) + sizeof(((type *)0)->field))

typedef enum {
    RIVER2_MPPT_CHG_TYPE_AUTO = 0,
    RIVER2_MPPT_CHG_TYPE_SOLAR = 1,
    RIVER2_MPPT_CHG_TYPE_CAR = 2,
} river2_mppt_chg_type_t;

#define RIVER2_PD_HEARTBEAT_SIZE 121

typedef struct {
    uint8_t model;
    uint8_t err_code[4];
    uint8_t sys_ver[4];
    uint8_t wifi_ver[4];
    uint8_t wifi_auto_rcvy;
    uint8_t soc;
    uint16_t watts_out_sum;
    uint16_t watts_in_sum;
    int32_t remain_time;
    uint8_t beep_mode;
    uint8_t dc_out_state;
    uint8_t usb1_watts;
    uint8_t usb2_watts;
    uint8_t qc_usb1_watts;
    uint8_t qc_usb2_watts;
    uint8_t typec1_watts;
    uint8_t typec2_watts;
    uint8_t typec1_temp;
    uint8_t typec2_temp;
    uint8_t car_state;
    uint8_t car_watts;
    uint8_t car_temp;
    uint16_t standby_min;
    uint16_t lcd_off_sec;
    uint8_t lcd_brightness;
    uint32_t chg_power_dc;
    uint32_t chg_sun_power;
    uint32_t chg_power_ac;
    uint32_t dsg_power_dc;
    uint32_t dsg_power_ac;
    uint32_t usb_used_time;
    uint32_t usbqc_used_time;
    uint32_t typec_used_time;
    uint32_t car_used_time;
    uint32_t inv_used_time;
    uint32_t dc_in_used_time;
    uint32_t mppt_used_time;
    uint8_t reserved[2];
    uint8_t ext_rj45_port;
    uint8_t ext_3p8_port;
    uint8_t ext_4p8_port;
    uint8_t sys_chg_dsg_state;
    uint8_t wifi_rssi;
    uint8_t wireless_watts;
    uint8_t screen_state[14];
    uint8_t ac_auto_config;
    uint8_t min_ac_out_soc;
    uint8_t ac_out_pause;
    uint8_t watth_is_config;
    uint8_t backup_soc;
    uint8_t hysteresis_add;
    uint32_t relay_switch_count;
} __attribute__((packed)) river2_pd_heartbeat_t;

_Static_assert(sizeof(river2_pd_heartbeat_t) == RIVER2_PD_HEARTBEAT_SIZE, "river2_pd_heartbeat_t layout mismatch");

#define RIVER2_EMS_HEARTBEAT_SIZE 46

typedef struct {
    uint8_t chg_state;
    uint8_t chg_cmd;
    uint8_t dsg_cmd;
    uint32_t chg_vol;
    uint32_t chg_amp;
    uint8_t fan_level;
    uint8_t max_charge_soc;
    uint8_t bms_model;
    uint8_t lcd_show_soc;
    uint8_t open_ups_flag;
    uint8_t bms_warning_state;
    uint32_t chg_remain_time;
    uint32_t dsg_remain_time;
    uint8_t ems_is_normal_flag;
    float f32_lcd_show_soc;
    uint8_t bms_is_connt[3];
    uint8_t max_available_num;
    uint8_t open_bms_idx;
    uint32_t para_vol_min;
    uint32_t para_vol_max;
    uint8_t min_dsg_soc;
    uint8_t open_oil_eb_soc;
    uint8_t close_oil_eb_soc;
} __attribute__((packed)) river2_ems_heartbeat_t;

_Static_assert(sizeof(river2_ems_heartbeat_t) == RIVER2_EMS_HEARTBEAT_SIZE, "river2_ems_heartbeat_t layout mismatch");

#define RIVER2_BMS_HEARTBEAT_SIZE 69

typedef struct {
    uint8_t num;
    uint8_t type;
    uint8_t cell_id;
    uint32_t err_code;
    uint32_t sys_ver;
    uint8_t soc;
    uint32_t vol;
    uint32_t amp;
    uint8_t temp;
    uint8_t open_bms_idx;
    uint32_t design_cap;
    uint32_t remain_cap;
    uint32_t full_cap;
    uint32_t cycles;
    uint8_t soh;
    uint16_t max_cell_vol;
    uint16_t min_cell_vol;
    uint8_t max_cell_temp;
    uint8_t min_cell_temp;
    uint8_t max_mos_temp;
    uint8_t min_mos_temp;
    uint8_t bms_fault;
    uint8_t bq_sys_stat_reg;
    uint32_t tag_chg_amp;
    float f32_show_soc;
    uint32_t input_watts;
    uint32_t output_watts;
    uint32_t remain_time;
} __attribute__((packed)) river2_bms_heartbeat_t;

_Static_assert(sizeof(river2_bms_heartbeat_t) == RIVER2_BMS_HEARTBEAT_SIZE, "river2_bms_heartbeat_t layout mismatch");

#define RIVER2_INVERTER_HEARTBEAT_SIZE 67

typedef struct {
    uint32_t err_code;
    uint32_t sys_ver;
    uint8_t charger_type;
    uint16_t input_watts;
    uint16_t output_watts;
    uint8_t inv_type;
    uint32_t inv_out_vol;
    uint32_t inv_out_amp;
    uint8_t inv_out_freq;
    uint32_t ac_in_vol;
    uint32_t ac_in_amp;
    uint8_t ac_in_freq;
    uint16_t out_temp;
    uint32_t dc_in_vol;
    uint32_t dc_in_amp;
    uint16_t dc_in_temp;
    uint8_t fan_state;
    uint8_t cfg_ac_enabled;
    uint8_t cfg_ac_xboost;
    uint32_t cfg_ac_out_voltage;
    uint8_t cfg_ac_out_freq;
    uint8_t cfg_ac_work_mode;
    uint8_t cfg_pause_flag;
    uint8_t ac_dip_switch;
    uint16_t cfg_fast_chg_watts;
    uint16_t cfg_slow_chg_watts;
    uint16_t standby_mins;
    uint8_t discharge_type;
    uint8_t ac_passby_auto_en;
    uint8_t pr_balance_mode;
    uint16_t ac_chg_rated_power;
    uint8_t cfg_gfci_enable;
} __attribute__((packed)) river2_inverter_heartbeat_t;

_Static_assert(sizeof(river2_inverter_heartbeat_t) == RIVER2_INVERTER_HEARTBEAT_SIZE,
               "river2_inverter_heartbeat_t layout mismatch");

#define RIVER2_MPPT_HEARTBEAT_SIZE 94

typedef struct {
    uint32_t fault_code;
    uint8_t sw_ver[4];
    uint32_t in_vol;
    uint32_t in_amp;
    uint16_t in_watts;
    uint32_t out_val;
    uint32_t out_amp;
    uint16_t out_watts;
    int16_t mppt_temp;
    uint8_t xt60_chg_type;
    uint8_t cfg_chg_type;
    uint8_t chg_type;
    uint8_t chg_state;
    uint32_t dcdc_12v_vol;
    uint32_t dcdc_12v_amp;
    uint16_t dcdc_12v_watts;
    uint32_t car_out_vol;
    uint32_t car_out_amp;
    uint16_t car_out_watts;
    int16_t car_temp;
    uint8_t car_state;
    int16_t dc24v_temp;
    uint8_t dc24v_state;
    uint8_t chg_pause_flag;
    uint32_t cfg_dc_chg_current;
    uint8_t beep_state;
    uint8_t cfg_ac_enabled;
    uint8_t cfg_ac_xboost;
    uint32_t cfg_ac_out_voltage;
    uint8_t cfg_ac_out_freq;
    uint16_t cfg_chg_watts;
    uint16_t ac_standby_mins;
    uint8_t discharge_type;
    uint16_t car_standby_mins;
    uint16_t power_standby_mins;
    uint16_t screen_standby_mins;
    uint16_t pay_flag;
    uint8_t reserved[8];
} __attribute__((packed)) river2_mppt_heartbeat_t;

_Static_assert(sizeof(river2_mppt_heartbeat_t) == RIVER2_MPPT_HEARTBEAT_SIZE, "river2_mppt_heartbeat_t layout mismatch");
