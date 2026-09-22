#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
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

/* Response can carry several mailboxes worth of envelopes, so it goes to
   the heap (PSRAM) rather than the task stack. */
#define RESP_MAX 49152   /* с запасом: живёт в PSRAM, не на стеке задачи */

/* Выделяем один раз на старте: повторный malloc/free такого размера каждые
   3 секунды фрагментирует кучу. */
static char *s_resp;

/* Экрану нужны только интеграции трекеров (у них tasks = true):
   ключ и название задачи. Обычная почта на дашборде не показывается. */
static void parse_tasks(cJSON *root)
{
    cJSON *boxes = cJSON_GetObjectItem(root, "mailboxes");
    if (!cJSON_IsArray(boxes)) {
        return;
    }

    task_list_t *lists = calloc(TASK_MAX_LISTS, sizeof(task_list_t));
    if (!lists) {
        return;
    }

    int n = 0;
    cJSON *box;
    cJSON_ArrayForEach(box, boxes) {
        if (n >= TASK_MAX_LISTS) break;
        if (!cJSON_IsTrue(cJSON_GetObjectItem(box, "tasks"))) {
            continue;
        }
        task_list_t *dst = &lists[n++];
        json_str(box, "id", dst->id, sizeof(dst->id));
        json_str(box, "label", dst->label, sizeof(dst->label));
        dst->total = (int)json_num(box, "unread", 0);

        cJSON *msgs = cJSON_GetObjectItem(box, "messages");
        cJSON *m;
        cJSON_ArrayForEach(m, msgs) {
            if (dst->count >= TASK_MAX_ITEMS) break;
            task_item_t *it = &dst->items[dst->count];
            json_str(m, "task", it->key, sizeof(it->key));
            json_str(m, "subject", it->title, sizeof(it->title));
            if (it->key[0]) {
                dst->count++;
            }
        }
    }

    bsp_display_lock(0);
    token_ui_set_tasks(lists, n);
    bsp_display_unlock();
    free(lists);
}

static bool poll_agent(void)
{
    bool ok = false;
    char *buf = s_resp;
    if (!buf) {
        ESP_LOGE(TAG, "no response buffer");
        return false;
    }

    esp_http_client_config_t cfg = {
        .url = AGENT_URL,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) {
        return false;
    }
#ifdef API_KEY
    esp_http_client_set_header(cl, "x-api-key", API_KEY);
#endif

    if (esp_http_client_open(cl, 0) == ESP_OK) {
        esp_http_client_fetch_headers(cl);
        int len = esp_http_client_read_response(cl, buf, RESP_MAX - 1);
        if (len > 0 && esp_http_client_get_status_code(cl) == 200) {
            buf[len] = '\0';
            cJSON *root = cJSON_Parse(buf);
            /* magic-qube nests usage under "claude"; the Python agent keeps
               the same fields at the root */
            cJSON *usage = cJSON_GetObjectItem(root, "claude");
            if (!cJSON_IsObject(usage)) {
                usage = root;
            }
            /* Ответ валиден сам по себе: расход Claude Code может ещё не
               прийти (ПК выключен), но почта и polza.ai уже есть. */
            if (root) {
                token_data_t d = {0};
                json_str(usage, "usage_status", d.usage_status,
                         sizeof(d.usage_status));
                /* цифрам верим, только если агент получил их от Anthropic */
                d.usage_ok = cJSON_IsNumber(cJSON_GetObjectItem(usage, "block_pct")) &&
                             (!d.usage_status[0] || strcmp(d.usage_status, "ok") == 0);
                d.block_pct      = (int)json_num(usage, "block_pct", 0);
                d.reset_min      = (int)json_num(usage, "reset_min", 0);
                d.week_pct       = (int)json_num(usage, "week_pct", 0);
                d.week_reset_min = (int)json_num(usage, "week_reset_min", 0);
                d.busy = cJSON_IsTrue(cJSON_GetObjectItem(usage, "busy"));

                d.weather_ok   = cJSON_IsNumber(cJSON_GetObjectItem(root, "temp_c"));
                d.temp_c       = json_num(root, "temp_c", 0);
                d.weather_code = (int)json_num(root, "weather_code", -1);
                /* без признака дня считаем, что день: солнце вместо луны */
                d.is_day = json_num(root, "is_day", 1) != 0;
                json_str(root, "sunrise", d.sunrise, sizeof(d.sunrise));
                json_str(root, "sunset", d.sunset, sizeof(d.sunset));
                json_str(root, "time", d.time, sizeof(d.time));
                json_str(root, "date", d.date, sizeof(d.date));

                cJSON *pz = cJSON_GetObjectItem(root, "polza");
                if (cJSON_IsObject(pz)) {
                    d.polza_ok      = true;
                    d.polza_balance = json_num(pz, "balance_rub",
                                      json_num(pz, "balanceRub", 0));
                }

                bsp_display_lock(0);
                token_ui_set_live(&d);
                bsp_display_unlock();
                parse_tasks(root);
                ok = true;
            }
            cJSON_Delete(root);
        }
        esp_http_client_close(cl);
    }
    esp_http_client_cleanup(cl);
    return ok;
}

/* ---- удаление задач: запрос уходит из отдельной задачи, чтобы не
   блокировать интерфейс ---- */

typedef struct {
    char list_id[32];
    char key[16];
} task_req_t;

static QueueHandle_t s_task_q;

/* DELETE /display/task/<box>/<taskKey> — снимает задачу из Mongo. */
static void delete_task(const task_req_t *req)
{
    char url[192];
    const char *base = AGENT_URL;
    const char *tail = strstr(base, "/display/state");
    size_t prefix = tail ? (size_t)(tail - base) : strlen(base);
    if (prefix >= sizeof(url)) {
        prefix = sizeof(url) - 1;
    }
    memcpy(url, base, prefix);
    url[prefix] = '\0';
    snprintf(url + prefix, sizeof(url) - prefix, "/display/task/%s/%s",
             req->list_id, req->key);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_DELETE,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) {
        return;
    }
#ifdef API_KEY
    esp_http_client_set_header(cl, "x-api-key", API_KEY);
#endif

    bool ok = false;
    if (esp_http_client_open(cl, 0) == ESP_OK) {
        esp_http_client_fetch_headers(cl);
        ok = esp_http_client_get_status_code(cl) == 200;
        esp_http_client_close(cl);
    }
    esp_http_client_cleanup(cl);
    ESP_LOGI(TAG, "delete task %s: %s", req->key, ok ? "ok" : "failed");

    bsp_display_lock(0);
    if (ok) {
        token_ui_remove_task(req->list_id, req->key);
    } else {
        token_ui_toast("Не удалось удалить", false);
    }
    bsp_display_unlock();
}

static void task_worker(void *arg)
{
    task_req_t req;
    for (;;) {
        if (xQueueReceive(s_task_q, &req, portMAX_DELAY) == pdTRUE) {
            delete_task(&req);
        }
    }
}

/* Вызывается из потока LVGL — блокировать нельзя. */
static void on_task_delete_req(const char *list_id, const char *key)
{
    task_req_t req = {0};
    strlcpy(req.list_id, list_id, sizeof(req.list_id));
    strlcpy(req.key, key, sizeof(req.key));
    xQueueSend(s_task_q, &req, 0);
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
    s_resp = heap_caps_malloc(RESP_MAX, MALLOC_CAP_SPIRAM);
    if (!s_resp) {
        s_resp = malloc(RESP_MAX);
    }

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

    s_task_q = xQueueCreate(2, sizeof(task_req_t));
    token_ui_set_task_delete_cb(on_task_delete_req);

    xTaskCreate(poll_task, "agent_poll", 6144, NULL, 5, NULL);
    xTaskCreate(task_worker, "task_delete", 6144, NULL, 5, NULL);
}
