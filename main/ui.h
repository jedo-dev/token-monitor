#pragma once

#include <stdbool.h>

/* Snapshot of everything the screen shows. Filled by net.c from the agent. */
typedef struct {
    bool  usage_ok;        /* пришёл ли расход Claude Code с ПК */
    int   block_pct;
    int   reset_min;
    int   week_pct;
    int   week_reset_min;
    long  tokens_today;
    double cost_usd;
    double temp_c;
    char  sunrise[8];
    char  sunset[8];
    char  time[8];
    char  date[16];
    bool  busy;

    /* last N days, oldest first */
    int   hist_len;
    long  hist_tokens[7];
    char  hist_label[7][8];

    /* polza.ai dashboard */
    bool   polza_ok;
    double polza_balance;
    double polza_today;
    int    polza_reqs_today;
    int    polza_reqs_total;
    int    polza_errors;
    char   polza_top_model[24];
    int    polza_hist_len;
    double polza_hist_cost[7];
    char   polza_hist_label[7][8];
} token_data_t;

/* Build the Token Monitor screen. Call with the LVGL display lock held. */
void token_ui_create(void);

/* Push live data from the PC agent. Call with the LVGL display lock held. */
void token_ui_set_live(const token_data_t *d);

/* Mark the connection as lost (keeps last values on screen). */
void token_ui_set_offline(void);

/* Battery level in percent, or -1 if unknown. */
void token_ui_set_battery(int pct);

/* ---------------- mail ---------------- */

#define MAIL_MAX_BOXES 6
#define MAIL_MAX_ITEMS 10

typedef struct {
    char uid[16];
    char from[48];
    char subject[104];
    char when[12];
    char task[16];     /* ключ задачи трекера, пусто если письмо обычное */
    bool seen;
} mail_item_t;

typedef struct {
    char id[32];
    char label[32];
    int  unread;
    int  count;
    bool tasks;        /* интеграция трекера: можно удалять задачи */
    mail_item_t items[MAIL_MAX_ITEMS];
} mailbox_t;

/* Refresh the mail page. Call with the LVGL display lock held. */
void token_ui_set_mail(const mailbox_t *boxes, int count);

/* Called when the user taps a message: net.c fetches the body. */
typedef void (*mail_open_cb_t)(const char *box_id, const char *uid);
void token_ui_set_mail_open_cb(mail_open_cb_t cb);

/* Called when the user taps the trash icon on a tracker message. */
typedef void (*task_delete_cb_t)(const char *box_id, const char *task_key);
void token_ui_set_task_delete_cb(task_delete_cb_t cb);

/* Short toast at the bottom of the screen. */
void token_ui_toast(const char *text, bool success);

/* Drop a row locally after the server confirmed the task is gone. */
void token_ui_remove_task(const char *box_id, const char *task_key);

/* Mark a message read locally right after its body was fetched. */
void token_ui_mark_seen(const char *box_id, const char *uid);

/* Show a fetched message body (or an error when text is NULL). */
void token_ui_show_message(const char *subject, const char *from,
                           const char *when, const char *text);

/* Settings persisted in NVS and editable on the Settings page. */
typedef struct {
    int  brightness;      /* 10..100 % */
    bool night_dim;       /* dim the panel at night */
    bool sound_alert;     /* warn when a quota crosses 90% */
} token_settings_t;

const token_settings_t *token_ui_settings(void);
