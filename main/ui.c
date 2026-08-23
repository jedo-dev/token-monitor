#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui.h"

/* Palette (dark theme) */
#define COL_BG      0x0D1117
#define COL_CARD    0x161B22
#define COL_BORDER  0x30363D
#define COL_TEXT    0xE6EDF3
#define COL_MUTED   0x8B949E
#define COL_ACCENT  0xD97757  /* Anthropic clay */
#define COL_GREEN   0x3FB950
#define COL_BLUE    0x58A6FF
#define COL_RED     0xF85149
#define COL_TRACK   0x21262D

#define SCR_W 480
#define SCR_H 480

/* Header */
static lv_obj_t *lbl_cat;
static lv_obj_t *lbl_title;
static lv_obj_t *lbl_clock;
static lv_obj_t *lbl_date;
static lv_obj_t *lbl_status;   /* mode: LIVE / DEMO / OFFLINE */

/* Info strip */
static lv_obj_t *lbl_sun;
static lv_obj_t *lbl_temp;
static lv_obj_t *lbl_batt;

/* Gauge cards */
typedef struct {
    lv_obj_t *pct;
    lv_obj_t *chip;
    lv_obj_t *bar;
    lv_obj_t *reset;
} gauge_t;
static gauge_t g_session, g_week;

/* Page 2 tiles */
static lv_obj_t *lbl_tokens;
static lv_obj_t *lbl_cost;

/* Footer */
static lv_obj_t *dots[2];
static lv_obj_t *lbl_activity;
static lv_obj_t *tv;

static bool is_live;
static lv_timer_t *demo_timer;

static void fmt_tokens(char *buf, size_t n, long tokens)
{
    if (tokens >= 1000000L) {
        snprintf(buf, n, "%.1fM", tokens / 1000000.0);
    } else if (tokens >= 1000L) {
        snprintf(buf, n, "%.0fK", tokens / 1000.0);
    } else {
        snprintf(buf, n, "%ld", tokens);
    }
}

/* "2h 15m" / "1d 12h" */
static void fmt_duration(char *buf, size_t n, int minutes)
{
    if (minutes <= 0) {
        snprintf(buf, n, "--");
    } else if (minutes >= 1440) {
        snprintf(buf, n, "%dd %dh", minutes / 1440, (minutes % 1440) / 60);
    } else if (minutes >= 60) {
        snprintf(buf, n, "%dh %02dm", minutes / 60, minutes % 60);
    } else {
        snprintf(buf, n, "%dm", minutes);
    }
}

static lv_color_t level_color(int pct)
{
    if (pct >= 90) return lv_color_hex(COL_RED);
    if (pct >= 70) return lv_color_hex(COL_ACCENT);
    return lv_color_hex(COL_GREEN);
}

static void gauge_set(gauge_t *g, int pct, int reset_min)
{
    char buf[48], dur[16];

    lv_label_set_text_fmt(g->pct, "%d%%", pct);
    lv_obj_set_style_text_color(g->pct, level_color(pct), 0);
    lv_bar_set_value(g->bar, pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(g->bar, level_color(pct), LV_PART_INDICATOR);

    fmt_duration(dur, sizeof(dur), reset_min);
    snprintf(buf, sizeof(buf), "Resets in %s", dur);
    lv_label_set_text(g->reset, buf);
}

/* ---------- construction helpers ---------- */

static void gauge_create(gauge_t *g, lv_obj_t *parent, int y, const char *chip_text)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, 12, y);
    lv_obj_set_size(card, SCR_W - 24, 104);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);

    g->pct = lv_label_create(card);
    lv_label_set_text(g->pct, "--%");
    lv_obj_set_style_text_font(g->pct, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(g->pct, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(g->pct, LV_ALIGN_TOP_LEFT, 0, -2);

    g->chip = lv_label_create(card);
    lv_label_set_text(g->chip, chip_text);
    lv_obj_set_style_text_font(g->chip, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g->chip, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_bg_color(g->chip, lv_color_hex(COL_TRACK), 0);
    lv_obj_set_style_bg_opa(g->chip, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(g->chip, 10, 0);
    lv_obj_set_style_pad_ver(g->chip, 4, 0);
    lv_obj_set_style_radius(g->chip, 10, 0);
    lv_obj_align(g->chip, LV_ALIGN_TOP_RIGHT, 0, 0);

    g->bar = lv_bar_create(card);
    lv_obj_set_size(g->bar, SCR_W - 48, 10);
    lv_obj_align(g->bar, LV_ALIGN_TOP_LEFT, 0, 48);
    lv_bar_set_range(g->bar, 0, 100);
    lv_obj_set_style_bg_color(g->bar, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g->bar, lv_color_hex(COL_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g->bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(g->bar, 5, LV_PART_INDICATOR);

    g->reset = lv_label_create(card);
    lv_label_set_text(g->reset, "Resets in --");
    lv_obj_set_style_text_font(g->reset, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g->reset, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(g->reset, LV_ALIGN_TOP_LEFT, 0, 66);
}

static lv_obj_t *stat_tile(lv_obj_t *parent, int x, int y, int w, int h,
                           const char *caption, lv_obj_t **value,
                           lv_color_t value_col)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, w, h);
    lv_obj_set_style_bg_color(tile, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, 12, 0);
    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_label_create(tile);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);

    *value = lv_label_create(tile);
    lv_label_set_text(*value, "--");
    lv_obj_set_style_text_color(*value, value_col, 0);
    lv_obj_set_style_text_font(*value, &lv_font_montserrat_26, 0);
    lv_obj_align(*value, LV_ALIGN_CENTER, 0, 6);
    return tile;
}

static void on_page_change(lv_event_t *e)
{
    (void)e;
    lv_obj_t *act = lv_tileview_get_tile_active(tv);
    int idx = (lv_obj_get_x(act) > 0) ? 1 : 0;
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(dots[i],
            lv_color_hex(i == idx ? COL_ACCENT : COL_BORDER), 0);
    }
}

/* Demo animation until the agent connects */
static void demo_tick(lv_timer_t *t)
{
    (void)t;
    static int p = 12;
    p = (p + 3) % 100;
    gauge_set(&g_session, p, 300 - p * 3);
    gauge_set(&g_week, 30 + p / 3, 4000 - p * 10);
}

void token_ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ---------- header ---------- */
    lbl_cat = lv_label_create(scr);
    lv_label_set_text(lbl_cat, "/\\_/\\\n( o.o )\n > ^ <");
    lv_obj_set_style_text_font(lbl_cat, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_cat, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_line_space(lbl_cat, 0, 0);
    lv_obj_align(lbl_cat, LV_ALIGN_TOP_LEFT, 14, 10);

    lbl_title = lv_label_create(scr);
    lv_label_set_text(lbl_title, "Claude Code");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(COL_ACCENT), 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 14);

    lbl_clock = lv_label_create(scr);
    lv_label_set_text(lbl_clock, "--:--");
    lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(lbl_clock, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(lbl_clock, LV_ALIGN_TOP_RIGHT, -14, 8);

    lbl_date = lv_label_create(scr);
    lv_label_set_text(lbl_date, "");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_date, LV_ALIGN_TOP_RIGHT, -14, 40);

    /* ---------- info strip ---------- */
    lbl_sun = lv_label_create(scr);
    lv_label_set_text(lbl_sun, LV_SYMBOL_UP " --:--   " LV_SYMBOL_DOWN " --:--");
    lv_obj_set_style_text_font(lbl_sun, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_sun, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_sun, LV_ALIGN_TOP_LEFT, 14, 62);

    lbl_temp = lv_label_create(scr);
    lv_label_set_text(lbl_temp, "--.- C");
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_temp, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_temp, LV_ALIGN_TOP_MID, 40, 62);

    lbl_batt = lv_label_create(scr);
    lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_set_style_text_font(lbl_batt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_batt, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_batt, LV_ALIGN_TOP_RIGHT, -14, 62);

    /* ---------- pages ---------- */
    tv = lv_tileview_create(scr);
    lv_obj_set_pos(tv, 0, 90);
    lv_obj_set_size(tv, SCR_W, 348);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tv, on_page_change, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *p1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_set_style_pad_all(p1, 0, 0);
    lv_obj_remove_flag(p1, LV_OBJ_FLAG_SCROLLABLE);
    gauge_create(&g_session, p1, 6, "Session");
    gauge_create(&g_week, p1, 122, "Weekly");

    lv_obj_t *p2 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT);
    lv_obj_set_style_pad_all(p2, 0, 0);
    lv_obj_remove_flag(p2, LV_OBJ_FLAG_SCROLLABLE);
    stat_tile(p2, 12, 6, 220, 104, "TOKENS TODAY", &lbl_tokens,
              lv_color_hex(COL_TEXT));
    stat_tile(p2, 248, 6, 220, 104, "COST TODAY", &lbl_cost,
              lv_color_hex(COL_GREEN));

    /* ---------- footer ---------- */
    for (int i = 0; i < 2; i++) {
        dots[i] = lv_obj_create(scr);
        lv_obj_set_size(dots[i], 8, 8);
        lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dots[i], 0, 0);
        lv_obj_set_style_bg_color(dots[i],
            lv_color_hex(i == 0 ? COL_ACCENT : COL_BORDER), 0);
        lv_obj_align(dots[i], LV_ALIGN_BOTTOM_LEFT, 16 + i * 16, -18);
        lv_obj_remove_flag(dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    lbl_activity = lv_label_create(scr);
    lv_label_set_text(lbl_activity, "Idle");
    lv_obj_set_style_text_font(lbl_activity, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_activity, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_activity, LV_ALIGN_BOTTOM_MID, 0, -14);

    lbl_status = lv_label_create(scr);
    lv_label_set_text(lbl_status, LV_SYMBOL_REFRESH " DEMO");
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(COL_ACCENT), 0);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_RIGHT, -14, -14);

    demo_timer = lv_timer_create(demo_tick, 700, NULL);
}

void token_ui_set_live(const token_data_t *d)
{
    char buf[64];

    if (!is_live) {
        is_live = true;
        lv_timer_pause(demo_timer);
        lv_label_set_text(lbl_status, LV_SYMBOL_WIFI " LIVE");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(COL_GREEN), 0);
    }

    gauge_set(&g_session, d->block_pct, d->reset_min);
    gauge_set(&g_week, d->week_pct, d->week_reset_min);

    fmt_tokens(buf, sizeof(buf), d->tokens_today);
    lv_label_set_text(lbl_tokens, buf);
    lv_label_set_text_fmt(lbl_cost, "$%.2f", d->cost_usd);

    lv_label_set_text(lbl_clock, d->time);
    lv_label_set_text(lbl_date, d->date);

    snprintf(buf, sizeof(buf), LV_SYMBOL_UP " %s   " LV_SYMBOL_DOWN " %s",
             d->sunrise, d->sunset);
    lv_label_set_text(lbl_sun, buf);
    lv_label_set_text_fmt(lbl_temp, "%.1f C", d->temp_c);

    lv_label_set_text(lbl_activity, d->busy ? "Thinking ..." : "Idle");
    lv_obj_set_style_text_color(lbl_activity,
        lv_color_hex(d->busy ? COL_BLUE : COL_MUTED), 0);
}

void token_ui_set_offline(void)
{
    lv_label_set_text(lbl_status, LV_SYMBOL_WARNING " OFFLINE");
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(COL_RED), 0);
}

void token_ui_set_battery(int pct)
{
    if (pct < 0) {
        lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_EMPTY " --%");
        return;
    }
    const char *sym = pct > 87 ? LV_SYMBOL_BATTERY_FULL :
                      pct > 62 ? LV_SYMBOL_BATTERY_3 :
                      pct > 37 ? LV_SYMBOL_BATTERY_2 :
                      pct > 12 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(lbl_batt, "%s %d%%", sym, pct);
    lv_obj_set_style_text_color(lbl_batt,
        lv_color_hex(pct <= 15 ? COL_RED : COL_MUTED), 0);
}
