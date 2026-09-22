#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "ui.h"

LV_FONT_DECLARE(font_ru_12);
LV_FONT_DECLARE(font_ru_16);
LV_FONT_DECLARE(font_ru_20);

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
#define COL_PURPLE  0x8B5CF6  /* polza.ai */
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

/* Page 2: polza.ai */
static lv_obj_t *lbl_pz_balance;
static lv_obj_t *lbl_pz_today;
static lv_obj_t *lbl_pz_reqs;
static lv_obj_t *lbl_pz_model;
static lv_obj_t *pz_chart;
static lv_chart_series_t *pz_ser;
static lv_obj_t *lbl_pz_days;
static lv_obj_t *lbl_pz_max;

/* Page 3: mail */
static lv_obj_t *mail_tabs;
static lv_obj_t *mail_list;
static lv_obj_t *lbl_mail_empty;
static mailbox_t s_boxes[MAIL_MAX_BOXES];
static int  s_box_count;
static int  s_box_active;
static mail_open_cb_t s_open_cb;
static task_delete_cb_t s_del_cb;
static lv_obj_t *toast;
static lv_obj_t *lbl_toast;

/* геометрия строки письма */
#define ROW_H   64
#define BTN_W   48
#define TEXT_W  (SCR_W - 24 - 22 - BTN_W - 16)

/* Message reader overlay */
static lv_obj_t *reader;
static lv_obj_t *lbl_read_subject;
static lv_obj_t *lbl_read_meta;
static lv_obj_t *lbl_read_body;

/* Page 4: settings */
static lv_obj_t *sl_bright;
static lv_obj_t *lbl_bright;

/* Footer */
#define PAGE_COUNT 4
static lv_obj_t *dots[PAGE_COUNT];
static lv_obj_t *lbl_activity;
static lv_obj_t *tv;

static token_settings_t s_settings = {
    .brightness = 80,
    .night_dim = true,
    .sound_alert = false,
};

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
    lv_obj_set_style_text_font(g->reset, &font_ru_16, 0);
    lv_obj_set_style_text_color(g->reset, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(g->reset, LV_ALIGN_TOP_LEFT, 0, 66);
}


static void on_page_change(lv_event_t *e)
{
    (void)e;
    lv_obj_t *act = lv_tileview_get_tile_active(tv);
    int idx = lv_obj_get_x(act) / SCR_W;
    if (idx < 0) idx = 0;
    if (idx >= PAGE_COUNT) idx = PAGE_COUNT - 1;
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_set_style_bg_color(dots[i],
            lv_color_hex(i == idx ? COL_ACCENT : COL_BORDER), 0);
    }
}

/* ---------- page 2: polza.ai dashboard ---------- */

static lv_obj_t *pz_tile(lv_obj_t *parent, int x, int y, int w,
                         const char *caption, lv_obj_t **value,
                         lv_color_t col)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, w, 76);
    lv_obj_set_style_bg_color(tile, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, 12, 0);
    lv_obj_set_style_pad_all(tile, 8, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_label_create(tile);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);

    *value = lv_label_create(tile);
    lv_label_set_text(*value, "--");
    lv_obj_set_style_text_font(*value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(*value, col, 0);
    lv_obj_align(*value, LV_ALIGN_BOTTOM_MID, 0, -2);
    return tile;
}

static void polza_page_create(lv_obj_t *parent)
{
    lv_obj_t *hdr = lv_label_create(parent);
    lv_label_set_text(hdr, "polza.ai");
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(COL_PURPLE), 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 14, 2);

    lbl_pz_model = lv_label_create(parent);
    lv_label_set_text(lbl_pz_model, "");
    lv_obj_set_style_text_font(lbl_pz_model, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_pz_model, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_pz_model, LV_ALIGN_TOP_RIGHT, -14, 6);

    pz_tile(parent, 12, 22, 148, "BALANCE", &lbl_pz_balance,
            lv_color_hex(COL_TEXT));
    pz_tile(parent, 166, 22, 148, "SPENT TODAY", &lbl_pz_today,
            lv_color_hex(COL_GREEN));
    pz_tile(parent, 320, 22, 148, "REQUESTS", &lbl_pz_reqs,
            lv_color_hex(COL_PURPLE));

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, 12, 108);
    lv_obj_set_size(card, SCR_W - 24, 176);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_label_create(card);
    lv_label_set_text(cap, "SPEND PER DAY, RUB");
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_pz_max = lv_label_create(card);
    lv_label_set_text(lbl_pz_max, "");
    lv_obj_set_style_text_font(lbl_pz_max, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_pz_max, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_pz_max, LV_ALIGN_TOP_RIGHT, 0, 0);

    pz_chart = lv_chart_create(card);
    lv_obj_set_size(pz_chart, SCR_W - 60, 104);
    lv_obj_align(pz_chart, LV_ALIGN_TOP_MID, 0, 22);
    lv_chart_set_type(pz_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(pz_chart, 7);
    lv_chart_set_range(pz_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(pz_chart, 3, 0);
    lv_obj_set_style_bg_opa(pz_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pz_chart, 0, 0);
    lv_obj_set_style_line_color(pz_chart, lv_color_hex(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_column(pz_chart, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(pz_chart, 3, LV_PART_ITEMS);
    pz_ser = lv_chart_add_series(pz_chart, lv_color_hex(COL_PURPLE),
                                 LV_CHART_AXIS_PRIMARY_Y);

    lbl_pz_days = lv_label_create(card);
    lv_label_set_text(lbl_pz_days, "");
    lv_obj_set_style_text_font(lbl_pz_days, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_pz_days, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_pz_days, LV_ALIGN_BOTTOM_MID, 0, 2);
}

static void polza_update(const token_data_t *d)
{
    if (!lbl_pz_balance) {
        return;
    }
    if (!d->polza_ok) {
        lv_label_set_text(lbl_pz_balance, "n/a");
        return;
    }

    lv_label_set_text_fmt(lbl_pz_balance, "%.0f R", d->polza_balance);
    lv_obj_set_style_text_color(lbl_pz_balance,
        lv_color_hex(d->polza_balance < 50 ? COL_RED : COL_TEXT), 0);
    lv_label_set_text_fmt(lbl_pz_today, "%.2f", d->polza_today);
    lv_label_set_text_fmt(lbl_pz_reqs, "%d", d->polza_reqs_today);
    if (d->polza_top_model[0]) {
        lv_label_set_text(lbl_pz_model, d->polza_top_model);
    }

    if (d->polza_hist_len > 0) {
        double max = 0.01;
        for (int i = 0; i < d->polza_hist_len; i++) {
            if (d->polza_hist_cost[i] > max) max = d->polza_hist_cost[i];
        }
        lv_chart_set_point_count(pz_chart, d->polza_hist_len);
        for (int i = 0; i < d->polza_hist_len; i++) {
            lv_chart_set_value_by_id(pz_chart, pz_ser, i,
                (int32_t)(d->polza_hist_cost[i] * 100 / max));
        }
        lv_chart_refresh(pz_chart);
        lv_label_set_text_fmt(lbl_pz_max, "max %.2f R", max);

        char line[96] = "";
        for (int i = 0; i < d->polza_hist_len; i++) {
            strlcat(line, d->polza_hist_label[i], sizeof(line));
            if (i + 1 < d->polza_hist_len) strlcat(line, "  ", sizeof(line));
        }
        lv_label_set_text(lbl_pz_days, line);
    }
}

/* ---------- page 3: mail ---------- */


static void mail_render_list(void);
static void mail_render_tabs(void);

static void on_reader_close(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(reader, LV_OBJ_FLAG_HIDDEN);
}

static void on_mail_item(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_box_active >= s_box_count) {
        return;
    }
    const mailbox_t *box = &s_boxes[s_box_active];
    if (idx < 0 || idx >= box->count) {
        return;
    }

    /* show the envelope immediately, body arrives asynchronously */
    token_ui_show_message(box->items[idx].subject, box->items[idx].from,
                          box->items[idx].when, NULL);
    if (s_open_cb) {
        s_open_cb(box->id, box->items[idx].uid);
    }
}

static void on_task_delete(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_box_active >= s_box_count) {
        return;
    }
    const mailbox_t *box = &s_boxes[s_box_active];
    if (idx < 0 || idx >= box->count || !box->items[idx].task[0]) {
        return;
    }
    if (s_del_cb) {
        s_del_cb(box->id, box->items[idx].task);
    }
}

void token_ui_remove_task(const char *box_id, const char *task_key)
{
    for (int b = 0; b < s_box_count; b++) {
        if (strcmp(s_boxes[b].id, box_id) != 0) {
            continue;
        }
        mailbox_t *box = &s_boxes[b];
        for (int i = 0; i < box->count; i++) {
            if (strcmp(box->items[i].task, task_key) != 0) {
                continue;
            }
            for (int j = i; j + 1 < box->count; j++) {
                box->items[j] = box->items[j + 1];
            }
            box->count--;
            if (box->unread > 0) {
                box->unread--;
            }
            if (b == s_box_active) {
                mail_render_list();
            } else {
                mail_render_tabs();
            }
            break;
        }
        break;
    }
    token_ui_toast(task_key, true);
}

/* Прочитанные письма на экране не нужны — убираем строку сразу после
   открытия. Задачи трекера (seen == true) не трогаем. */
void token_ui_mark_seen(const char *box_id, const char *uid)
{
    for (int b = 0; b < s_box_count; b++) {
        if (strcmp(s_boxes[b].id, box_id) != 0) {
            continue;
        }
        mailbox_t *box = &s_boxes[b];
        if (box->tasks) {
            return;
        }
        for (int i = 0; i < box->count; i++) {
            if (strcmp(box->items[i].uid, uid) != 0) {
                continue;
            }
            for (int j = i; j + 1 < box->count; j++) {
                box->items[j] = box->items[j + 1];
            }
            box->count--;
            if (box->unread > 0) {
                box->unread--;
            }
            if (b == s_box_active) {
                mail_render_list();
            } else {
                mail_render_tabs();
            }
            return;
        }
        return;
    }
}

static void on_mail_tab(lv_event_t *e)
{
    s_box_active = (int)(intptr_t)lv_event_get_user_data(e);
    mail_render_list();
}

static void mail_render_tabs(void)
{
    lv_obj_clean(mail_tabs);
    if (s_box_count <= 1) {
        return;
    }
    int x = 0;
    for (int i = 0; i < s_box_count; i++) {
        lv_obj_t *btn = lv_button_create(mail_tabs);
        lv_obj_set_pos(btn, x, 0);
        lv_obj_set_height(btn, 30);
        lv_obj_set_style_bg_color(btn, lv_color_hex(
            i == s_box_active ? COL_ACCENT : COL_TRACK), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_pad_hor(btn, 12, 0);
        lv_obj_add_event_cb(btn, on_mail_tab, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "%s  %d", s_boxes[i].label, s_boxes[i].unread);
        lv_obj_set_style_text_font(lbl, &font_ru_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
        lv_obj_center(lbl);

        lv_obj_update_layout(btn);
        x += lv_obj_get_width(btn) + 8;
    }
}

static void mail_render_list(void)
{
    mail_render_tabs();
    lv_obj_clean(mail_list);

    if (s_box_count == 0 || s_boxes[s_box_active].count == 0) {
        lv_obj_add_flag(mail_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_mail_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(mail_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lbl_mail_empty, LV_OBJ_FLAG_HIDDEN);

    const mailbox_t *box = &s_boxes[s_box_active];
    for (int i = 0; i < box->count; i++) {
        const mail_item_t *m = &box->items[i];
        bool has_task = box->tasks && m->task[0];

        lv_obj_t *row = lv_obj_create(mail_list);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, ROW_H);
        lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(COL_BORDER), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_margin_bottom(row, 6, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* левая колонка: метка непрочитанного */
        if (!m->seen) {
            lv_obj_t *dot = lv_obj_create(row);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(COL_ACCENT), 0);
            lv_obj_align(dot, LV_ALIGN_LEFT_MID, 8, 0);
        }

        /* текстовый блок фиксированной ширины, чтобы не наезжал на кнопки */
        int text_w = TEXT_W - (has_task ? BTN_W : 0);

        lv_obj_t *from = lv_label_create(row);
        lv_label_set_text(from, m->from);
        lv_obj_set_style_text_font(from, &font_ru_12, 0);
        lv_obj_set_style_text_color(from, lv_color_hex(
            m->seen ? COL_MUTED : COL_ACCENT), 0);
        lv_obj_set_width(from, text_w - 46);
        lv_label_set_long_mode(from, LV_LABEL_LONG_DOT);
        lv_obj_align(from, LV_ALIGN_TOP_LEFT, 22, 8);

        lv_obj_t *when = lv_label_create(row);
        lv_label_set_text(when, m->when);
        lv_obj_set_style_text_font(when, &font_ru_12, 0);
        lv_obj_set_style_text_color(when, lv_color_hex(COL_MUTED), 0);
        lv_obj_align(when, LV_ALIGN_TOP_LEFT, 22 + text_w - 42, 8);

        lv_obj_t *subj = lv_label_create(row);
        lv_label_set_text(subj, m->subject);
        lv_obj_set_style_text_font(subj, &font_ru_16, 0);
        lv_obj_set_style_text_color(subj, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_width(subj, text_w);
        lv_label_set_long_mode(subj, LV_LABEL_LONG_DOT);
        lv_obj_align(subj, LV_ALIGN_TOP_LEFT, 22, 28);

        /* кнопка «прочитать» */
        lv_obj_t *open = lv_button_create(row);
        lv_obj_set_size(open, 40, 40);
        lv_obj_align(open, LV_ALIGN_RIGHT_MID, has_task ? -50 : -8, 0);
        lv_obj_set_style_bg_color(open, lv_color_hex(COL_TRACK), 0);
        lv_obj_set_style_radius(open, 8, 0);
        lv_obj_add_event_cb(open, on_mail_item, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_t *oi = lv_label_create(open);
        lv_label_set_text(oi, LV_SYMBOL_EYE_OPEN);
        lv_obj_set_style_text_color(oi, lv_color_hex(COL_BLUE), 0);
        lv_obj_center(oi);

        /* кнопка «удалить задачу» — только для трекера и Jira */
        if (has_task) {
            lv_obj_t *del = lv_button_create(row);
            lv_obj_set_size(del, 40, 40);
            lv_obj_align(del, LV_ALIGN_RIGHT_MID, -8, 0);
            lv_obj_set_style_bg_color(del, lv_color_hex(COL_TRACK), 0);
            lv_obj_set_style_radius(del, 8, 0);
            lv_obj_add_event_cb(del, on_task_delete, LV_EVENT_CLICKED,
                                (void *)(intptr_t)i);
            lv_obj_t *di = lv_label_create(del);
            lv_label_set_text(di, LV_SYMBOL_TRASH);
            lv_obj_set_style_text_color(di, lv_color_hex(COL_RED), 0);
            lv_obj_center(di);
        }
    }
}

static void mail_page_create(lv_obj_t *parent)
{
    lv_obj_t *hdr = lv_label_create(parent);
    lv_label_set_text(hdr, "Почта");
    lv_obj_set_style_text_font(hdr, &font_ru_16, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(COL_BLUE), 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 12, 6);

    mail_tabs = lv_obj_create(parent);
    lv_obj_set_pos(mail_tabs, 80, 0);
    lv_obj_set_size(mail_tabs, SCR_W - 92, 34);
    lv_obj_set_style_bg_opa(mail_tabs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mail_tabs, 0, 0);
    lv_obj_set_style_pad_all(mail_tabs, 0, 0);
    lv_obj_set_scroll_dir(mail_tabs, LV_DIR_HOR);   /* вкладок может быть много */
    lv_obj_set_scrollbar_mode(mail_tabs, LV_SCROLLBAR_MODE_OFF);

    mail_list = lv_obj_create(parent);
    lv_obj_set_pos(mail_list, 12, 40);
    lv_obj_set_size(mail_list, SCR_W - 24, 306);
    lv_obj_set_style_bg_opa(mail_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mail_list, 0, 0);
    lv_obj_set_style_pad_all(mail_list, 0, 0);
    lv_obj_set_flex_flow(mail_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(mail_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(mail_list, LV_SCROLLBAR_MODE_AUTO);

    lbl_mail_empty = lv_label_create(parent);
    lv_label_set_text(lbl_mail_empty, "Нет писем");
    lv_obj_set_style_text_font(lbl_mail_empty, &font_ru_16, 0);
    lv_obj_set_style_text_color(lbl_mail_empty, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_mail_empty, LV_ALIGN_CENTER, 0, 0);
}

/* ---------- message reader (full-screen overlay) ---------- */

static void reader_create(void)
{
    reader = lv_obj_create(lv_layer_top());
    lv_obj_set_size(reader, SCR_W, SCR_H);
    lv_obj_set_pos(reader, 0, 0);
    lv_obj_set_style_bg_color(reader, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_border_width(reader, 0, 0);
    lv_obj_set_style_radius(reader, 0, 0);
    lv_obj_set_style_pad_all(reader, 12, 0);
    lv_obj_remove_flag(reader, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(reader, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *back = lv_button_create(reader);
    lv_obj_set_size(back, 92, 34);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(COL_TRACK), 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, on_reader_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *blbl = lv_label_create(back);
    lv_label_set_text(blbl, LV_SYMBOL_LEFT " Назад");
    lv_obj_set_style_text_font(blbl, &font_ru_12, 0);
    lv_obj_set_style_text_color(blbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(blbl);

    lbl_read_meta = lv_label_create(reader);
    lv_label_set_text(lbl_read_meta, "");
    lv_obj_set_style_text_font(lbl_read_meta, &font_ru_12, 0);
    lv_obj_set_style_text_color(lbl_read_meta, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_width(lbl_read_meta, 340);
    lv_label_set_long_mode(lbl_read_meta, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_read_meta, LV_ALIGN_TOP_RIGHT, 0, 8);

    lbl_read_subject = lv_label_create(reader);
    lv_label_set_text(lbl_read_subject, "");
    lv_obj_set_style_text_font(lbl_read_subject, &font_ru_20, 0);
    lv_obj_set_style_text_color(lbl_read_subject, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_width(lbl_read_subject, SCR_W - 24);
    lv_label_set_long_mode(lbl_read_subject, LV_LABEL_LONG_WRAP);
    lv_obj_align(lbl_read_subject, LV_ALIGN_TOP_LEFT, 0, 44);

    lv_obj_t *body_box = lv_obj_create(reader);
    lv_obj_set_size(body_box, SCR_W - 24, 340);
    lv_obj_align(body_box, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(body_box, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_color(body_box, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(body_box, 1, 0);
    lv_obj_set_style_radius(body_box, 10, 0);
    lv_obj_set_style_pad_all(body_box, 10, 0);
    lv_obj_set_scroll_dir(body_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body_box, LV_SCROLLBAR_MODE_AUTO);

    lbl_read_body = lv_label_create(body_box);
    lv_label_set_text(lbl_read_body, "");
    lv_obj_set_style_text_font(lbl_read_body, &font_ru_16, 0);
    lv_obj_set_style_text_color(lbl_read_body, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_width(lbl_read_body, SCR_W - 50);
    lv_label_set_long_mode(lbl_read_body, LV_LABEL_LONG_WRAP);
}

void token_ui_set_mail(const mailbox_t *boxes, int count)
{
    if (count > MAIL_MAX_BOXES) count = MAIL_MAX_BOXES;
    if (count < 0) count = 0;

    /* Данные приходят каждые 3 секунды, но почта меняется редко.
       Без этой проверки список пересоздавался бы постоянно — экран мигал,
       а память бесконечно перевыделялась. */
    if (count == s_box_count &&
        memcmp(s_boxes, boxes, sizeof(mailbox_t) * count) == 0) {
        return;
    }

    memcpy(s_boxes, boxes, sizeof(mailbox_t) * count);
    s_box_count = count;
    if (s_box_active >= count) {
        s_box_active = 0;
    }
    if (mail_list) {
        mail_render_list();
    }
}

void token_ui_set_mail_open_cb(mail_open_cb_t cb)
{
    s_open_cb = cb;
}

void token_ui_set_task_delete_cb(task_delete_cb_t cb)
{
    s_del_cb = cb;
}

static void toast_hide(lv_timer_t *t)
{
    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_delete(t);
}

void token_ui_toast(const char *text, bool success)
{
    if (!toast) {
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), success ? "Задача %s удалена" : "%s", text);
    lv_label_set_text(lbl_toast, buf);
    lv_obj_set_style_bg_color(toast,
        lv_color_hex(success ? COL_GREEN : COL_RED), 0);
    lv_obj_remove_flag(toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(toast_hide, 2200, NULL);
}

void token_ui_show_message(const char *subject, const char *from,
                           const char *when, const char *text)
{
    if (!reader) {
        return;
    }
    if (subject) {
        lv_label_set_text(lbl_read_subject, subject);
    }
    if (from) {
        char meta[80];
        snprintf(meta, sizeof(meta), "%s   %s", from, when ? when : "");
        lv_label_set_text(lbl_read_meta, meta);
    }
    lv_label_set_text(lbl_read_body, text ? text : "Загрузка ...");
    lv_obj_scroll_to_y(lv_obj_get_parent(lbl_read_body), 0, LV_ANIM_OFF);
    lv_obj_remove_flag(reader, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- page 4: settings ---------- */

static void on_brightness(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    s_settings.brightness = (int)lv_slider_get_value(sl);
    lv_label_set_text_fmt(lbl_bright, "%d%%", s_settings.brightness);
    bsp_display_brightness_set(s_settings.brightness);
}

static void on_night_dim(lv_event_t *e)
{
    s_settings.night_dim = lv_obj_has_state(lv_event_get_target(e),
                                            LV_STATE_CHECKED);
}

static void on_sound(lv_event_t *e)
{
    s_settings.sound_alert = lv_obj_has_state(lv_event_get_target(e),
                                              LV_STATE_CHECKED);
}

static lv_obj_t *settings_row(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_pos(row, 12, y);
    lv_obj_set_size(row, SCR_W - 24, 58);
    lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_color(row, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 10, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    return row;
}

static void settings_page_create(lv_obj_t *parent)
{
    lv_obj_t *row = settings_row(parent, 6, LV_SYMBOL_SETTINGS "  Brightness");

    lbl_bright = lv_label_create(row);
    lv_label_set_text_fmt(lbl_bright, "%d%%", s_settings.brightness);
    lv_obj_set_style_text_font(lbl_bright, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_bright, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_bright, LV_ALIGN_RIGHT_MID, 0, 0);

    sl_bright = lv_slider_create(row);
    lv_obj_set_size(sl_bright, 200, 8);
    lv_obj_align(sl_bright, LV_ALIGN_RIGHT_MID, -58, 0);
    lv_slider_set_range(sl_bright, 10, 100);
    lv_slider_set_value(sl_bright, s_settings.brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl_bright, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl_bright, lv_color_hex(COL_ACCENT),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl_bright, lv_color_hex(COL_ACCENT), LV_PART_KNOB);
    lv_obj_add_event_cb(sl_bright, on_brightness, LV_EVENT_VALUE_CHANGED, NULL);

    row = settings_row(parent, 72, LV_SYMBOL_EYE_CLOSE "  Night dim");
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_GREEN),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (s_settings.night_dim) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, on_night_dim, LV_EVENT_VALUE_CHANGED, NULL);

    row = settings_row(parent, 138, LV_SYMBOL_BELL "  Alert at 90%");
    sw = lv_switch_create(row);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_GREEN),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (s_settings.sound_alert) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, on_sound, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *info = lv_label_create(parent);
    lv_label_set_text(info, "Token Monitor  ESP32-S3-Touch-LCD-4 Rev4.0");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -6);
}

const token_settings_t *token_ui_settings(void)
{
    return &s_settings;
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
    lv_obj_align(lbl_cat, LV_ALIGN_TOP_LEFT, 12, 8);

    lbl_title = lv_label_create(scr);
    lv_label_set_text(lbl_title, "Claude Code");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(COL_ACCENT), 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 12);

    lbl_clock = lv_label_create(scr);
    lv_label_set_text(lbl_clock, "--:--");
    lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(lbl_clock, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(lbl_clock, LV_ALIGN_TOP_RIGHT, -12, 6);

    lbl_date = lv_label_create(scr);
    lv_label_set_text(lbl_date, "");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_date, LV_ALIGN_TOP_RIGHT, -12, 38);

    /* ---------- info strip ---------- */
    lbl_sun = lv_label_create(scr);
    lv_label_set_text(lbl_sun, LV_SYMBOL_UP " --:--   " LV_SYMBOL_DOWN " --:--");
    lv_obj_set_style_text_font(lbl_sun, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_sun, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_sun, LV_ALIGN_TOP_LEFT, 12, 60);

    lbl_temp = lv_label_create(scr);
    lv_label_set_text(lbl_temp, "--.- C");
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_temp, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_temp, LV_ALIGN_TOP_MID, 0, 60);

    lbl_batt = lv_label_create(scr);
    lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_set_style_text_font(lbl_batt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_batt, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_batt, LV_ALIGN_TOP_RIGHT, -12, 60);

    /* ---------- pages ---------- */
    tv = lv_tileview_create(scr);
    lv_obj_set_pos(tv, 0, 86);
    lv_obj_set_size(tv, SCR_W, 352);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tv, on_page_change, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *p1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_set_style_pad_all(p1, 0, 0);
    lv_obj_remove_flag(p1, LV_OBJ_FLAG_SCROLLABLE);
    gauge_create(&g_session, p1, 6, "Session");
    gauge_create(&g_week, p1, 122, "Weekly");

    lv_obj_t *p2 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    lv_obj_set_style_pad_all(p2, 0, 0);
    lv_obj_remove_flag(p2, LV_OBJ_FLAG_SCROLLABLE);
    polza_page_create(p2);

    lv_obj_t *p3 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
    lv_obj_set_style_pad_all(p3, 0, 0);
    lv_obj_remove_flag(p3, LV_OBJ_FLAG_SCROLLABLE);
    mail_page_create(p3);

    lv_obj_t *p4 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_LEFT);
    lv_obj_set_style_pad_all(p4, 0, 0);
    lv_obj_remove_flag(p4, LV_OBJ_FLAG_SCROLLABLE);
    settings_page_create(p4);

    reader_create();

    /* ---------- footer ---------- */
    for (int i = 0; i < PAGE_COUNT; i++) {
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

    /* всплывающее уведомление поверх всего */
    toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(toast, 300, 44);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_radius(toast, 10, 0);
    lv_obj_set_style_border_width(toast, 0, 0);
    lv_obj_set_style_pad_all(toast, 8, 0);
    lv_obj_remove_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
    lbl_toast = lv_label_create(toast);
    lv_obj_set_style_text_font(lbl_toast, &font_ru_16, 0);
    lv_obj_set_style_text_color(lbl_toast, lv_color_hex(COL_BG), 0);
    lv_obj_center(lbl_toast);

    demo_timer = lv_timer_create(demo_tick, 700, NULL);
}

void token_ui_set_live(const token_data_t *d)
{
    char buf[64];

    if (!is_live) {
        is_live = true;
        lv_timer_pause(demo_timer);
    }
    /* метку ставим каждый раз: после OFFLINE она должна вернуться в LIVE */
    lv_label_set_text(lbl_status, LV_SYMBOL_WIFI " LIVE");
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(COL_GREEN), 0);

    if (d->usage_ok) {
        gauge_set(&g_session, d->block_pct, d->reset_min);
        gauge_set(&g_week, d->week_pct, d->week_reset_min);
    } else {
        /* ПК не присылает статистику — честно показываем это вместо старых цифр */
        lv_label_set_text(g_session.pct, "--");
        lv_label_set_text(g_week.pct, "--");
        /* подсказываем, что сделать, а не просто «нет данных» */
        const char *why = "нет данных с ПК";
        if (strcmp(d->usage_status, "token_expired") == 0) {
            why = "Токен истёк: запустите claude";
        } else if (strcmp(d->usage_status, "no_token") == 0) {
            why = "Нет входа: claude /login";
        } else if (strcmp(d->usage_status, "unavailable") == 0) {
            why = "Anthropic не отвечает";
        }
        lv_label_set_text(g_session.reset, why);
        lv_label_set_text(g_week.reset, "");
        lv_bar_set_value(g_session.bar, 0, LV_ANIM_OFF);
        lv_bar_set_value(g_week.bar, 0, LV_ANIM_OFF);
    }

    lv_label_set_text(lbl_clock, d->time);
    lv_label_set_text(lbl_date, d->date);

    snprintf(buf, sizeof(buf), LV_SYMBOL_UP " %s   " LV_SYMBOL_DOWN " %s",
             d->sunrise, d->sunset);
    lv_label_set_text(lbl_sun, buf);
    lv_label_set_text_fmt(lbl_temp, "%.1f C", d->temp_c);

    polza_update(d);

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
