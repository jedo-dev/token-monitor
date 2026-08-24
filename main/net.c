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
#define RESP_MAX 24576

static void parse_mail(cJSON *root)
{
    cJSON *boxes = cJSON_GetObjectItem(root, "mailboxes");
    if (!cJSON_IsArray(boxes)) {
        return;
    }

    mailbox_t *list = calloc(MAIL_MAX_BOXES, sizeof(mailbox_t));
    if (!list) {
        return;
    }

    int n = 0;
    cJSON *box;
    cJSON_ArrayForEach(box, boxes) {
        if (n >= MAIL_MAX_BOXES) break;
        mailbox_t *dst = &list[n];
        json_str(box, "id", dst->id, sizeof(dst->id));
        json_str(box, "label", dst->label, sizeof(dst->label));
        dst->unread = (int)json_num(box, "unread", 0);

        cJSON *msgs = cJSON_GetObjectItem(box, "messages");
        if (cJSON_IsArray(msgs)) {
            cJSON *m;
            cJSON_ArrayForEach(m, msgs) {
                if (dst->count >= MAIL_MAX_ITEMS) break;
                mail_item_t *it = &dst->items[dst->count];
                json_str(m, "uid", it->uid, sizeof(it->uid));
                json_str(m, "from", it->from, sizeof(it->from));
                json_str(m, "subject", it->subject, sizeof(it->subject));
                json_str(m, "when", it->when, sizeof(it->when));
                it->seen = cJSON_IsTrue(cJSON_GetObjectItem(m, "seen"));
                dst->count++;
            }
        }
        n++;
    }

    bsp_display_lock(0);
    token_ui_set_mail(list, n);
    bsp_display_unlock();
    free(list);
}

static bool poll_agent(void)
{
    bool ok = false;
    char *buf = malloc(RESP_MAX);
    if (!buf) {
        ESP_LOGE(TAG, "out of memory for response buffer");
        return false;
    }

    esp_http_client_config_t cfg = {
        .url = AGENT_URL,
        .timeout_ms = 2500,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) {
        free(buf);
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
            if (root && cJSON_IsNumber(cJSON_GetObjectItem(usage, "block_pct"))) {
                token_data_t d = {0};
                d.block_pct      = (int)json_num(usage, "block_pct", 0);
                d.reset_min      = (int)json_num(usage, "reset_min", 0);
                d.week_pct       = (int)json_num(usage, "week_pct", 0);
                d.week_reset_min = (int)json_num(usage, "week_reset_min", 0);
                d.tokens_today   = (long)json_num(usage, "tokens_today", 0);
                d.cost_usd       = json_num(usage, "cost_today_usd", 0);
                d.temp_c         = json_num(root, "temp_c", 0);
                d.busy = cJSON_IsTrue(cJSON_GetObjectItem(usage, "busy"));
                json_str(root, "sunrise", d.sunrise, sizeof(d.sunrise));
                json_str(root, "sunset", d.sunset, sizeof(d.sunset));
                json_str(root, "time", d.time, sizeof(d.time));
                json_str(root, "date", d.date, sizeof(d.date));

                cJSON *pz = cJSON_GetObjectItem(root, "polza");
                if (cJSON_IsObject(pz)) {
                    d.polza_ok         = true;
                    d.polza_balance    = json_num(pz, "balance_rub",
                                         json_num(pz, "balanceRub", 0));
                    d.polza_today      = json_num(pz, "spent_today_rub",
                                         json_num(pz, "spentTodayRub", 0));
                    d.polza_reqs_today = (int)json_num(pz, "requests_today",
                                         json_num(pz, "requestsToday", 0));
                    d.polza_reqs_total = (int)json_num(pz, "requests_total",
                                         json_num(pz, "requestsTotal", 0));
                    d.polza_errors     = (int)json_num(pz, "errors", 0);
                    json_str(pz, "top_model", d.polza_top_model,
                             sizeof(d.polza_top_model));
                    json_str(pz, "topModel", d.polza_top_model,
                             sizeof(d.polza_top_model));

                    cJSON *ph = cJSON_GetObjectItem(pz, "history");
                    if (cJSON_IsArray(ph)) {
                        int n = cJSON_GetArraySize(ph);
                        int skip = n > 7 ? n - 7 : 0;
                        for (int i = skip; i < n; i++) {
                            cJSON *day = cJSON_GetArrayItem(ph, i);
                            int k = d.polza_hist_len;
                            d.polza_hist_cost[k] = json_num(day, "c", 0);
                            json_str(day, "d", d.polza_hist_label[k],
                                     sizeof(d.polza_hist_label[k]));
                            d.polza_hist_len++;
                        }
                    }
                }

                cJSON *hist = cJSON_GetObjectItem(root, "history");
                if (cJSON_IsArray(hist)) {
                    int n = cJSON_GetArraySize(hist);
                    int skip = n > 7 ? n - 7 : 0;   /* keep the newest 7 */
                    for (int i = skip; i < n; i++) {
                        cJSON *day = cJSON_GetArrayItem(hist, i);
                        int k = d.hist_len;
                        d.hist_tokens[k] = (long)json_num(day, "t", 0);
                        json_str(day, "d", d.hist_label[k],
                                 sizeof(d.hist_label[k]));
                        d.hist_len++;
                    }
                }

                bsp_display_lock(0);
                token_ui_set_live(&d);
                bsp_display_unlock();
                parse_mail(root);
                ok = true;
            }
            cJSON_Delete(root);
        }
        esp_http_client_close(cl);
    }
    esp_http_client_cleanup(cl);
    free(buf);
    return ok;
}

/* ---- on-demand message body ---- */

typedef struct {
    char box_id[32];
    char uid[16];
} mail_req_t;

static QueueHandle_t s_mail_q;

/* Build ".../display/mail/<box>/<uid>" from the configured state URL. */
static void build_mail_url(char *out, size_t n, const mail_req_t *req)
{
    const char *base = AGENT_URL;
    const char *tail = strstr(base, "/display/state");
    if (!tail) {
        tail = strstr(base, "/stats");
    }
    size_t prefix = tail ? (size_t)(tail - base) : strlen(base);
    if (prefix >= n) {
        prefix = n - 1;
    }
    memcpy(out, base, prefix);
    out[prefix] = '\0';
    snprintf(out + prefix, n - prefix, "/display/mail/%s/%s",
             req->box_id, req->uid);
}

static void fetch_body(const mail_req_t *req)
{
    char url[192];
    build_mail_url(url, sizeof(url), req);

    char *buf = malloc(RESP_MAX);
    if (!buf) {
        return;
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) {
        free(buf);
        return;
    }
#ifdef API_KEY
    esp_http_client_set_header(cl, "x-api-key", API_KEY);
#endif

    const char *text = NULL;
    cJSON *root = NULL;

    if (esp_http_client_open(cl, 0) == ESP_OK) {
        esp_http_client_fetch_headers(cl);
        int len = esp_http_client_read_response(cl, buf, RESP_MAX - 1);
        if (len > 0 && esp_http_client_get_status_code(cl) == 200) {
            buf[len] = '\0';
            root = cJSON_Parse(buf);
            cJSON *t = root ? cJSON_GetObjectItem(root, "text") : NULL;
            if (cJSON_IsString(t)) {
                text = t->valuestring;
            }
        }
        esp_http_client_close(cl);
    }
    esp_http_client_cleanup(cl);

    bsp_display_lock(0);
    token_ui_show_message(NULL, NULL, NULL,
                          text ? text : "Не удалось загрузить письмо");
    bsp_display_unlock();

    cJSON_Delete(root);
    free(buf);
}

static void mail_task(void *arg)
{
    mail_req_t req;
    for (;;) {
        if (xQueueReceive(s_mail_q, &req, portMAX_DELAY) == pdTRUE) {
            fetch_body(&req);
        }
    }
}

/* Called from the LVGL thread — must not block. */
static void on_mail_open(const char *box_id, const char *uid)
{
    mail_req_t req = {0};
    strlcpy(req.box_id, box_id, sizeof(req.box_id));
    strlcpy(req.uid, uid, sizeof(req.uid));
    xQueueSend(s_mail_q, &req, 0);
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

    s_mail_q = xQueueCreate(2, sizeof(mail_req_t));
    token_ui_set_mail_open_cb(on_mail_open);

    xTaskCreate(poll_task, "agent_poll", 6144, NULL, 5, NULL);
    xTaskCreate(mail_task, "mail_body", 6144, NULL, 5, NULL);
}
