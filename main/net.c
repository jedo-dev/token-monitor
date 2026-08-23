#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "bsp/esp-bsp.h"
#include "ui.h"
#include "net.h"
#include "secrets.h"

static const char *TAG = "net";

#define WIFI_CONNECTED_BIT BIT0
#define POLL_PERIOD_MS     3000
#define OFFLINE_AFTER_FAILS 3

static EventGroupHandle_t s_ev;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_ev, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_ev, WIFI_CONNECTED_BIT);
    }
}

/* Fetch AGENT_URL and push parsed values into the UI.
   Returns true on success. */
static void json_str(cJSON *root, const char *key, char *dst, size_t n)
{
    cJSON *it = cJSON_GetObjectItem(root, key);
    if (cJSON_IsString(it) && it->valuestring) {
        strlcpy(dst, it->valuestring, n);
    }
}

static double json_num(cJSON *root, const char *key, double dflt)
{
    cJSON *it = cJSON_GetObjectItem(root, key);
    return cJSON_IsNumber(it) ? it->valuedouble : dflt;
}

static bool poll_agent(void)
{
    char buf[768];
    bool ok = false;

    esp_http_client_config_t cfg = {
        .url = AGENT_URL,
        .timeout_ms = 2500,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) {
        return false;
    }

    if (esp_http_client_open(cl, 0) == ESP_OK) {
        esp_http_client_fetch_headers(cl);
        int len = esp_http_client_read_response(cl, buf, sizeof(buf) - 1);
        if (len > 0 && esp_http_client_get_status_code(cl) == 200) {
            buf[len] = '\0';
            cJSON *root = cJSON_Parse(buf);
            if (root && cJSON_IsNumber(cJSON_GetObjectItem(root, "block_pct"))) {
                token_data_t d = {0};
                d.block_pct      = (int)json_num(root, "block_pct", 0);
                d.reset_min      = (int)json_num(root, "reset_min", 0);
                d.week_pct       = (int)json_num(root, "week_pct", 0);
                d.week_reset_min = (int)json_num(root, "week_reset_min", 0);
                d.tokens_today   = (long)json_num(root, "tokens_today", 0);
                d.cost_usd       = json_num(root, "cost_today_usd", 0);
                d.temp_c         = json_num(root, "temp_c", 0);
                d.busy = cJSON_IsTrue(cJSON_GetObjectItem(root, "busy"));
                json_str(root, "sunrise", d.sunrise, sizeof(d.sunrise));
                json_str(root, "sunset", d.sunset, sizeof(d.sunset));
                json_str(root, "time", d.time, sizeof(d.time));
                json_str(root, "date", d.date, sizeof(d.date));

                bsp_display_lock(0);
                token_ui_set_live(&d);
                bsp_display_unlock();
                ok = true;
            }
            cJSON_Delete(root);
        }
        esp_http_client_close(cl);
    }
    esp_http_client_cleanup(cl);
    return ok;
}

static void poll_task(void *arg)
{
    int fails = 0;
    for (;;) {
        xEventGroupWaitBits(s_ev, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                            portMAX_DELAY);
        if (poll_agent()) {
            fails = 0;
        } else if (++fails == OFFLINE_AFTER_FAILS) {
            ESP_LOGW(TAG, "Agent unreachable, going OFFLINE");
            bsp_display_lock(0);
            token_ui_set_offline();
            bsp_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

void net_start(void)
{
    s_ev = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, WIFI_SSID, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, WIFI_PASS, sizeof(sta.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(poll_task, "agent_poll", 6144, NULL, 5, NULL);
}
