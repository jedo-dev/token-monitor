#pragma once

#include <stdbool.h>

/* Snapshot of everything the screen shows. Filled by net.c from the agent. */
typedef struct {
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

/* Settings persisted in NVS and editable on the Settings page. */
typedef struct {
    int  brightness;      /* 10..100 % */
    bool night_dim;       /* dim the panel at night */
    bool sound_alert;     /* warn when a quota crosses 90% */
} token_settings_t;

const token_settings_t *token_ui_settings(void);
