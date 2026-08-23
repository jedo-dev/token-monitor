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
} token_data_t;

/* Build the Token Monitor screen. Call with the LVGL display lock held. */
void token_ui_create(void);

/* Push live data from the PC agent. Call with the LVGL display lock held. */
void token_ui_set_live(const token_data_t *d);

/* Mark the connection as lost (keeps last values on screen). */
void token_ui_set_offline(void);

/* Battery level in percent, or -1 if unknown. */
void token_ui_set_battery(int pct);
