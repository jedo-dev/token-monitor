#include <stdio.h>
#include <stdlib.h>
#include "lvgl.h"
#include "ui.h"

/* Palette */
#define COL_BG      0x0D1117
#define COL_TILE    0x161B22
#define COL_BORDER  0x30363D
#define COL_TEXT    0xE6EDF3
#define COL_MUTED   0x8B949E
#define COL_ACCENT  0xD97757  /* Anthropic clay */
#define COL_GREEN   0x3FB950
#define COL_TRACK   0x21262D

/* Demo data (later replaced by data from the PC agent) */
typedef struct {
    int block_pct;        /* % of 5h block used */
    long tokens_today;    /* tokens spent today */
    int reset_min;        /* minutes until block reset */
    int week_pct;         /* % of weekly limit */
} demo_state_t;

static demo_state_t st = {
    .block_pct = 37,
    .tokens_today = 8400000L,
    .reset_min = 133,
    .week_pct = 38,
};

static lv_obj_t *arc_block;
static lv_obj_t *lbl_pct;
static lv_obj_t *lbl_tokens;
static lv_obj_t *lbl_cost;
static lv_obj_t *lbl_reset;
static lv_obj_t *bar_week;
static lv_obj_t *lbl_week;

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

static void refresh_widgets(void)
{
    char buf[32];

    lv_arc_set_value(arc_block, st.block_pct);
    lv_label_set_text_fmt(lbl_pct, "%d%%", st.block_pct);

    fmt_tokens(buf, sizeof(buf), st.tokens_today);
    lv_label_set_text(lbl_tokens, buf);

    snprintf(buf, sizeof(buf), "$%.2f", st.tokens_today * 15.0 / 1000000.0);
    lv_label_set_text(lbl_cost, buf);

    lv_label_set_text_fmt(lbl_reset, "%d:%02d", st.reset_min / 60, st.reset_min % 60);

    lv_bar_set_value(bar_week, st.week_pct, LV_ANIM_ON);
    lv_label_set_text_fmt(lbl_week, "Week  %d%%", st.week_pct);
}

/* Simulate live usage until the real data feed is connected */
static void demo_tick(lv_timer_t *timer)
{
    (void)timer;

    st.tokens_today += 5000 + rand() % 40000;
    if (rand() % 3 == 0) st.block_pct++;
    if (st.block_pct > 100) st.block_pct = 0;
    if (--st.reset_min <= 0) st.reset_min = 300;
    st.week_pct = 30 + st.block_pct / 4;

    refresh_widgets();
}

static lv_obj_t *make_tile(lv_obj_t *parent, int x, int w, const char *title,
                           lv_obj_t **value_lbl, lv_color_t value_col)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_pos(tile, x, 336);
    lv_obj_set_size(tile, w, 84);
    lv_obj_set_style_bg_color(tile, lv_color_hex(COL_TILE), 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, 10, 0);
    lv_obj_set_style_pad_all(tile, 8, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_label_create(tile);
    lv_label_set_text(cap, title);
    lv_obj_set_style_text_color(cap, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);

    *value_lbl = lv_label_create(tile);
    lv_label_set_text(*value_lbl, "--");
    lv_obj_set_style_text_color(*value_lbl, value_col, 0);
    lv_obj_set_style_text_font(*value_lbl, &lv_font_montserrat_26, 0);
    lv_obj_align(*value_lbl, LV_ALIGN_BOTTOM_MID, 0, -4);

    return tile;
}

void token_ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Header */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "TOKEN MONITOR");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 14);

    lv_obj_t *mode = lv_label_create(scr);
    lv_label_set_text(mode, LV_SYMBOL_REFRESH "  DEMO");
    lv_obj_set_style_text_color(mode, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_text_font(mode, &lv_font_montserrat_16, 0);
    lv_obj_align(mode, LV_ALIGN_TOP_RIGHT, -16, 16);

    /* 5h-block usage arc */
    arc_block = lv_arc_create(scr);
    lv_obj_set_size(arc_block, 250, 250);
    lv_obj_align(arc_block, LV_ALIGN_TOP_MID, 0, 60);
    lv_arc_set_rotation(arc_block, 270);
    lv_arc_set_bg_angles(arc_block, 0, 360);
    lv_arc_set_range(arc_block, 0, 100);
    lv_obj_remove_style(arc_block, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc_block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc_block, 18, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_block, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_block, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_block, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_block, true, LV_PART_INDICATOR);

    lbl_pct = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_pct, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(lbl_pct, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_pct, LV_ALIGN_TOP_MID, 0, 155);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "of 5h block");
    lv_obj_set_style_text_color(sub, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 210);

    /* Stat tiles */
    make_tile(scr, 16, 145, "TOKENS TODAY", &lbl_tokens, lv_color_hex(COL_TEXT));
    make_tile(scr, 168, 145, "COST TODAY", &lbl_cost, lv_color_hex(COL_GREEN));
    make_tile(scr, 320, 145, "RESET IN", &lbl_reset, lv_color_hex(COL_ACCENT));

    /* Weekly limit bar */
    lbl_week = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_week, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_font(lbl_week, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_week, LV_ALIGN_BOTTOM_LEFT, 16, -34);

    bar_week = lv_bar_create(scr);
    lv_obj_set_size(bar_week, 448, 12);
    lv_obj_align(bar_week, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_bar_set_range(bar_week, 0, 100);
    lv_obj_set_style_bg_color(bar_week, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_week, lv_color_hex(COL_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_week, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_week, 6, LV_PART_INDICATOR);

    refresh_widgets();

    lv_timer_create(demo_tick, 1000, NULL);
}
