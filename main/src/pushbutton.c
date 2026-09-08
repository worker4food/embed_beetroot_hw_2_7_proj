#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>

#include "pushbutton.h"

#define PUSHBUTTON_DEBOUNCE_US 25000

static IRAM_ATTR void pushbutton_isr_handler(void *arg)
{
    pushbutton_t *btn = (pushbutton_t *)arg;
    esp_timer_stop(btn->debounce_timer);
    esp_timer_start_once(btn->debounce_timer, PUSHBUTTON_DEBOUNCE_US);
}

static void pushbutton_debounce_cb(void *arg)
{
    pushbutton_t *btn = (pushbutton_t *)arg;
    if (gpio_get_level(btn->pin) == 1) {
        xTaskNotify(btn->owner_task, btn->event_mask, eSetBits);
    }
}

esp_err_t pushbutton_init(gpio_num_t pin, uint32_t event_mask, pushbutton_t *btn)
{
    if (btn == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    btn->pin = pin;
    btn->event_mask = event_mask;
    btn->owner_task = xTaskGetCurrentTaskHandle();

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    esp_err_t r = gpio_config(&cfg);
    if (r != ESP_OK) {
        ESP_LOGE(__func__, "gpio_config() failed for pin %d: %s", pin, esp_err_to_name(r));
        return r;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = pushbutton_debounce_cb,
        .arg = btn,
        .name = "pushbutton",
    };
    r = esp_timer_create(&timer_args, &btn->debounce_timer);
    if (r != ESP_OK) {
        ESP_LOGE(__func__, "esp_timer_create() failed: %s", esp_err_to_name(r));
        return r;
    }

    r = gpio_isr_handler_add(pin, pushbutton_isr_handler, btn);
    if (r != ESP_OK) {
        ESP_LOGE(__func__, "gpio_isr_handler_add() failed for pin %d: %s", pin, esp_err_to_name(r));
        esp_timer_delete(btn->debounce_timer);
        return r;
    }

    return ESP_OK;
}
