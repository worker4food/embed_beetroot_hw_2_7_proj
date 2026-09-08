#pragma once

#include <stdint.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

typedef struct pushbutton {
    gpio_num_t pin;
    uint32_t event_mask;
    TaskHandle_t owner_task;
    esp_timer_handle_t debounce_timer;
} pushbutton_t;

esp_err_t pushbutton_init(gpio_num_t pin, uint32_t event_mask, pushbutton_t *btn);
