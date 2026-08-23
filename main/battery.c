#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "custom_io_expander_ch32v003.h"
#include "battery.h"
#include "ui.h"

static const char *TAG = "battery";

/* Rev 4.0 reads the pack through the CH32V003 IO expander's 10-bit ADC
   behind a 1:3 divider. */
#define ADC_MAX        1023.0f
#define ADC_REF_V      3.3f
#define DIVIDER_RATIO  3.0f
#define V_EMPTY        3.30f
#define V_FULL         4.20f

static esp_io_expander_handle_t s_expander;

static int voltage_to_pct(float v)
{
    float pct = (v - V_EMPTY) * 100.0f / (V_FULL - V_EMPTY);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (int)(pct + 0.5f);
}

static void battery_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t total = 0;
        int good = 0;
        for (int i = 0; i < 8; i++) {
            uint16_t sample = 0;
            if (custom_io_expander_get_adc(s_expander, &sample) == ESP_OK) {
                total += sample;
                good++;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        int pct = -1;
        if (good) {
            float v = (total / (float)good) * ADC_REF_V / ADC_MAX * DIVIDER_RATIO;
            pct = voltage_to_pct(v);
            ESP_LOGD(TAG, "%.2f V -> %d%%", v, pct);
        }
        bsp_display_lock(0);
        token_ui_set_battery(pct);
        bsp_display_unlock();

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void battery_start(void)
{
    s_expander = bsp_io_expander_init();
    if (!s_expander) {
        ESP_LOGW(TAG, "IO expander not available, battery readout disabled");
        return;
    }
    xTaskCreate(battery_task, "battery", 3072, NULL, 3, NULL);
}
