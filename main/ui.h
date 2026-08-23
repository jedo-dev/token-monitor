#pragma once

/* Build the Token Monitor screen. Call with the LVGL display lock held. */
void token_ui_create(void);

/* Push live data from the PC agent. Call with the LVGL display lock held. */
void token_ui_set_live(int block_pct, long tokens_today, double cost_usd,
                       int reset_min, int week_pct);

/* Mark the connection as lost (keeps last values on screen). */
void token_ui_set_offline(void);
