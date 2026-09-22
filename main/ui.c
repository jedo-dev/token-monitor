/* Дашборд 480×480: один экран из карточек, без перелистывания.
   Координаты и размеры — из спецификации макета Claude Design
   («Token Monitor — спецификация для LVGL 9»). */

#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui.h"

LV_FONT_DECLARE(tm_m12);    /* Montserrat 12 Medium, с кириллицей */
LV_FONT_DECLARE(tm_m16);    /* Montserrat 16 Medium, с кириллицей */
LV_FONT_DECLARE(tm_sb20);   /* Montserrat 20 SemiBold, заголовок */
LV_FONT_DECLARE(tm_b36);    /* Montserrat 36 Bold, цифры */
LV_FONT_DECLARE(tm_b48);    /* Montserrat 48 Bold, цифры */

extern const uint8_t weather_icons[] asm("_binary_weather_bin_start");
extern const uint8_t sunrise_icon[]  asm("_binary_sunrise_bin_start");
extern const uint8_t sunset_icon[]   asm("_binary_sunset_bin_start");
extern const uint8_t close_icon[]    asm("_binary_close_bin_start");
extern const uint8_t check_icon[]    asm("_binary_check_bin_start");
extern const uint8_t logo_frames[]   asm("_binary_logo_bin_start");

#define COL_BG      0x0D1117
#define COL_CARD    0x161B22
#define COL_LINE    0x30363D
#define COL_TEXT    0xE6EDF3
#define COL_MUTED   0x8B949E
#define COL_ACCENT  0xD97757
#define COL_GREEN   0x3FB950
#define COL_RED     0xF85149
#define COL_PURPLE  0x8B5CF6

#define LOGO_SIZE      112
#define LOGO_FRAMES    12
#define LOGO_FRAME_MS  70
#define WEATHER_ICONS  8
#define POLZA_LOW_RUB  50

/* порядок совпадает с tools/export_icons.py */
enum { W_CLEAR, W_NIGHT, W_PARTLY, W_CLOUDY, W_RAIN, W_SNOW, W_THUNDER, W_FOG };

typedef struct {
    lv_obj_t *card;
    lv_obj_t *pct;
    lv_obj_t *track;
    lv_obj_t *fill;
    lv_obj_t *reset_cap;
    lv_obj_t *reset;
    lv_obj_t *hint;          /* «Токен истёк…» вместо цифр */
} limit_card_t;

typedef struct {
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *pill;
    lv_obj_t *pill_lbl;
    lv_obj_t *more;
    lv_obj_t *rows;          /* сюда пересоздаются строки задач */
    task_list_t data;
    bool has_data;
} task_card_t;

static lv_obj_t *dot, *lbl_link, *batt_fill, *lbl_batt;
static limit_card_t lim_block, lim_week;
static lv_obj_t *logo_img;
static lv_timer_t *logo_timer;
static int logo_frame;
static lv_obj_t *weather_img, *lbl_temp, *lbl_sunrise, *lbl_sunset;
static lv_obj_t *lbl_clock, *lbl_date;
static lv_obj_t *lbl_balance, *lbl_balance_sub;
static task_card_t tasks_tracker, tasks_jira;
static lv_obj_t *toast;

static lv_image_dsc_t weather_dsc[WEATHER_ICONS];
static lv_image_dsc_t sunrise_dsc, sunset_dsc, close_dsc, check_dsc;
static lv_image_dsc_t logo_dsc[LOGO_FRAMES];

static task_delete_cb_t s_del_cb;

/* ---------- помощники ---------- */

static void icon_dsc(lv_image_dsc_t *d, const uint8_t *data, int w, int h,
                     lv_color_format_t cf)
{
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = cf;
    d->header.w = w;
    d->header.h = h;
    d->header.stride = w * 2;
    d->data_size = w * h * (cf == LV_COLOR_FORMAT_RGB565A8 ? 3 : 2);
    d->data = data;
}

static lv_obj_t *card_create(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(COL_LINE), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/* Метка в коробке из спецификации. Там высота строки задана как в CSS
   (текст по центру строки), а у шрифта своя высота строки — поэтому
   ставим метку так, чтобы центры строк совпали. */
static lv_obj_t *label_box(lv_obj_t *parent, const lv_font_t *font,
                           uint32_t color, int x, int y, int w, int h,
                           lv_text_align_t align)
{
    lv_obj_t *l = lv_label_create(parent);
    int lh = lv_font_get_line_height(font);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_pos(l, x, y + (h - lh) / 2);
    lv_obj_set_size(l, w, lh);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *image_at(lv_obj_t *parent, const lv_image_dsc_t *src, int x, int y)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_set_pos(img, x, y);
    return img;
}

static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h,
                      uint32_t color, int radius)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_size(r, w, h);
    lv_obj_set_style_bg_color(r, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, radius, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return r;
}

static uint32_t level_color(int pct)
{
    if (pct > 90) return COL_RED;
    if (pct >= 70) return COL_ACCENT;
    return COL_GREEN;
}

/* «2 ч 15 мин», «6 д 12 ч», «45 мин» */
static void fmt_reset(char *buf, size_t n, int minutes)
{
    if (minutes <= 0) {
        snprintf(buf, n, "—");
    } else if (minutes >= 1440) {
        snprintf(buf, n, "%d д %d ч", minutes / 1440, (minutes % 1440) / 60);
    } else if (minutes >= 60) {
        snprintf(buf, n, "%d ч %d мин", minutes / 60, minutes % 60);
    } else {
        snprintf(buf, n, "%d мин", minutes);
    }
}

/* «Tue Sep 22» → «вт, 22 сен». Часы на плате не идут, дату присылает сервер. */
static void fmt_date(char *out, size_t n, const char *en)
{
    static const char *const DAYS_EN[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static const char *const DAYS_RU[] = {"пн", "вт", "ср", "чт", "пт", "сб", "вс"};
    static const char *const MON_EN[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *const MON_RU[] = {"янв", "фев", "мар", "апр", "мая", "июн",
                                         "июл", "авг", "сен", "окт", "ноя", "дек"};
    char day[4] = "", mon[4] = "";
    int dd = 0;
    if (sscanf(en, "%3s %3s %d", day, mon, &dd) != 3) {
        snprintf(out, n, "%s", en);
        return;
    }
    const char *d_ru = day, *m_ru = mon;
    for (int i = 0; i < 7; i++) if (!strcmp(day, DAYS_EN[i])) d_ru = DAYS_RU[i];
    for (int i = 0; i < 12; i++) if (!strcmp(mon, MON_EN[i])) m_ru = MON_RU[i];
    snprintf(out, n, "%s, %d %s", d_ru, dd, m_ru);
}

/* WMO-код open-meteo → иконка */
static int weather_icon(int code, bool is_day)
{
    if (code < 0) return W_CLOUDY;
    if (code == 0) return is_day ? W_CLEAR : W_NIGHT;
    if (code <= 2) return is_day ? W_PARTLY : W_CLOUDY;
    if (code == 3) return W_CLOUDY;
    if (code == 45 || code == 48) return W_FOG;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return W_SNOW;
    if (code >= 95) return W_THUNDER;
    return W_RAIN;           /* морось, дождь, ливни */
}

/* ---------- шапка ---------- */

static void header_create(lv_obj_t *scr)
{
    lv_obj_t *title = label_box(scr, &tm_sb20, COL_TEXT, 8, 8, 240, 32,
                                LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(title, "Token Monitor");

    dot = rect(scr, 354, 20, 8, 8, COL_GREEN, 4);
    lbl_link = label_box(scr, &tm_m12, COL_GREEN, 338, 8, 60, 32,
                         LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_style_text_letter_space(lbl_link, 1, 0);
    lv_label_set_text(lbl_link, "LIVE");

    /* батарея: корпус 21×12 с обводкой, заливка по заряду, контакт 2×5 */
    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 414, 18);
    lv_obj_set_size(body, 21, 12);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_radius(body, 3, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    batt_fill = rect(scr, 416, 20, 0, 8, COL_MUTED, 1);
    rect(scr, 436, 21, 2, 6, COL_MUTED, 1);

    lbl_batt = label_box(scr, &tm_m12, COL_MUTED, 444, 8, 28, 32,
                         LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(lbl_batt, "--%");
}

/* ---------- лимиты ---------- */

static void limit_create(limit_card_t *c, lv_obj_t *scr, int x, const char *caption)
{
    c->card = card_create(scr, x, 48, 149, 149);

    lv_obj_t *cap = label_box(c->card, &tm_m16, COL_MUTED, 12, 12, 125, 20,
                              LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(cap, caption);

    c->pct = label_box(c->card, &tm_b48, COL_TEXT, 12, 32, 125, 48,
                       LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_letter_space(c->pct, -1, 0);
    lv_label_set_text(c->pct, "--%");

    c->track = rect(c->card, 12, 88, 125, 6, COL_LINE, 3);
    c->fill = rect(c->card, 12, 88, 0, 6, COL_GREEN, 3);

    c->reset_cap = label_box(c->card, &tm_m12, COL_MUTED, 12, 101, 125, 16,
                             LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(c->reset_cap, "сброс через");
    c->reset = label_box(c->card, &tm_m16, COL_TEXT, 12, 117, 125, 20,
                         LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(c->reset, "—");

    /* подсказка на месте цифр: три строки по 20 px */
    c->hint = lv_label_create(c->card);
    lv_obj_set_style_text_font(c->hint, &tm_m16, 0);
    lv_obj_set_style_text_color(c->hint, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_line_space(c->hint, 20 - lv_font_get_line_height(&tm_m16), 0);
    lv_obj_set_pos(c->hint, 12, 44);
    lv_obj_set_width(c->hint, 125);
    lv_obj_add_flag(c->hint, LV_OBJ_FLAG_HIDDEN);
}

static void limit_set(limit_card_t *c, int pct, int reset_min)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    uint32_t col = level_color(pct);
    char buf[32];

    lv_obj_add_flag(c->hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(c->pct, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(c->track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(c->fill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(c->reset_cap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(c->reset, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text_fmt(c->pct, "%d%%", pct);
    lv_obj_set_style_text_color(c->pct, lv_color_hex(col), 0);
    lv_obj_set_width(c->fill, 125 * pct / 100);
    lv_obj_set_style_bg_color(c->fill, lv_color_hex(col), 0);
    fmt_reset(buf, sizeof(buf), reset_min);
    lv_label_set_text(c->reset, buf);
}

/* Вместо цифр — подсказка, что делать. */
static void limit_hint(limit_card_t *c, const char *text)
{
    lv_obj_add_flag(c->pct, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c->track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c->fill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c->reset_cap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c->reset, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(c->hint, text);
    lv_obj_remove_flag(c->hint, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- логотип: неподвижен в простое, «думает», пока Claude работает ---------- */

static void logo_tick(lv_timer_t *t)
{
    (void)t;
    logo_frame = (logo_frame + 1) % LOGO_FRAMES;
    lv_image_set_src(logo_img, &logo_dsc[logo_frame]);
}

static void logo_set_busy(bool busy)
{
    if (busy && !logo_timer) {
        logo_timer = lv_timer_create(logo_tick, LOGO_FRAME_MS, NULL);
    } else if (!busy && logo_timer) {
        lv_timer_delete(logo_timer);
        logo_timer = NULL;
        logo_frame = 0;
        lv_image_set_src(logo_img, &logo_dsc[0]);
    }
}

/* ---------- задачи ---------- */

static void on_task_close(lv_event_t *e)
{
    task_card_t *c = lv_event_get_user_data(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (!c->has_data || idx < 0 || idx >= c->data.count || !s_del_cb) {
        return;
    }
    s_del_cb(c->data.id, c->data.items[idx].key);
}

static void task_card_create(task_card_t *c, lv_obj_t *scr, int x, const char *title)
{
    c->card = card_create(scr, x, 362, 228, 110);

    /* заголовок и счётчик — flex-строка с зазором 8 */
    lv_obj_t *head = lv_obj_create(c->card);
    lv_obj_set_pos(head, 12, 6);
    lv_obj_set_size(head, LV_SIZE_CONTENT, 20);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_set_style_pad_column(head, 8, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    c->title = lv_label_create(head);
    lv_obj_set_style_text_font(c->title, &tm_m16, 0);
    lv_obj_set_style_text_color(c->title, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(c->title, title);

    c->pill = lv_obj_create(head);
    lv_obj_set_size(c->pill, LV_SIZE_CONTENT, 18);
    lv_obj_set_style_min_width(c->pill, 24, 0);
    lv_obj_set_style_bg_color(c->pill, lv_color_hex(COL_LINE), 0);
    lv_obj_set_style_border_width(c->pill, 0, 0);
    lv_obj_set_style_radius(c->pill, 9, 0);
    lv_obj_set_style_pad_hor(c->pill, 7, 0);
    lv_obj_set_style_pad_ver(c->pill, 0, 0);
    lv_obj_remove_flag(c->pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    c->pill_lbl = lv_label_create(c->pill);
    lv_obj_set_style_text_font(c->pill_lbl, &tm_m12, 0);
    lv_obj_set_style_text_color(c->pill_lbl, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(c->pill_lbl, "0");
    lv_obj_center(c->pill_lbl);

    c->more = label_box(c->card, &tm_m12, COL_MUTED, 156, 6, 60, 20,
                        LV_TEXT_ALIGN_RIGHT);

    c->rows = lv_obj_create(c->card);
    lv_obj_set_pos(c->rows, 0, 28);
    lv_obj_set_size(c->rows, 228, 80);
    lv_obj_set_style_bg_opa(c->rows, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c->rows, 0, 0);
    lv_obj_set_style_pad_all(c->rows, 0, 0);
    lv_obj_remove_flag(c->rows, LV_OBJ_FLAG_SCROLLABLE);
}

static void task_card_render(task_card_t *c)
{
    lv_obj_clean(c->rows);
    const task_list_t *t = &c->data;
    int total = c->has_data ? t->total : 0;
    if (total < t->count) total = t->count;

    lv_label_set_text_fmt(c->pill_lbl, "%d", total);
    int shown = t->count < 2 ? t->count : 2;
    if (total > shown) {
        lv_label_set_text_fmt(c->more, "ещё %d", total - shown);
    } else {
        lv_label_set_text(c->more, "");
    }

    if (!c->has_data || shown == 0) {
        /* пусто: галочка и «Задач нет» по центру */
        lv_obj_t *box = lv_obj_create(c->rows);
        lv_obj_set_size(box, 228, 80);
        lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_pad_all(box, 0, 0);
        lv_obj_set_style_pad_column(box, 8, 0);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_image_set_src(lv_image_create(box), &check_dsc);
        lv_obj_t *l = lv_label_create(box);
        lv_obj_set_style_text_font(l, &tm_m16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
        lv_label_set_text(l, "Задач нет");
        return;
    }

    for (int n = 0; n < shown; n++) {
        int y = 40 * n;                      /* внутри rows: 28 + 40·n от карточки */
        const task_item_t *it = &t->items[n];

        lv_obj_t *key = label_box(c->rows, &tm_m12, COL_ACCENT, 12, y + 3, 160, 14,
                                  LV_TEXT_ALIGN_LEFT);
        lv_label_set_text(key, it->key);

        lv_obj_t *title = label_box(c->rows, &tm_m16, COL_TEXT, 12, y + 17, 160, 20,
                                    LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_label_set_text(title, it->title);

        lv_obj_t *btn = lv_button_create(c->rows);
        lv_obj_set_pos(btn, 176, y);
        lv_obj_set_size(btn, 40, 40);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COL_LINE), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_user_data(btn, (void *)(intptr_t)n);
        lv_obj_add_event_cb(btn, on_task_close, LV_EVENT_CLICKED, c);
        lv_obj_center(image_at(btn, &close_dsc, 0, 0));

        if (n == 0 && shown > 1) {
            rect(c->rows, 12, 40, 204, 1, COL_LINE, 0);   /* y 68 от карточки */
        }
    }
}

static task_card_t *card_for_label(const char *label)
{
    if (strstr(label, "Tracker")) return &tasks_tracker;
    if (strstr(label, "Jira")) return &tasks_jira;
    return NULL;
}

/* ---------- публичное ---------- */

void token_ui_create(void)
{
    icon_dsc(&sunrise_dsc, sunrise_icon, 16, 16, LV_COLOR_FORMAT_RGB565A8);
    icon_dsc(&sunset_dsc, sunset_icon, 16, 16, LV_COLOR_FORMAT_RGB565A8);
    icon_dsc(&close_dsc, close_icon, 16, 16, LV_COLOR_FORMAT_RGB565A8);
    icon_dsc(&check_dsc, check_icon, 24, 24, LV_COLOR_FORMAT_RGB565A8);
    for (int i = 0; i < WEATHER_ICONS; i++) {
        icon_dsc(&weather_dsc[i], weather_icons + i * 32 * 32 * 3, 32, 32,
                 LV_COLOR_FORMAT_RGB565A8);
    }
    for (int i = 0; i < LOGO_FRAMES; i++) {
        icon_dsc(&logo_dsc[i], logo_frames + i * LOGO_SIZE * LOGO_SIZE * 2,
                 LOGO_SIZE, LOGO_SIZE, LV_COLOR_FORMAT_RGB565);
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    header_create(scr);

    /* ряд 1: лимиты и логотип */
    limit_create(&lim_block, scr, 8, "5 часов");
    limit_create(&lim_week, scr, 165, "Неделя");
    lv_obj_t *logo_card = card_create(scr, 322, 48, 150, 149);
    logo_img = image_at(logo_card, &logo_dsc[0], 19, 18);

    /* ряд 2: погода, время, polza.ai */
    lv_obj_t *wc = card_create(scr, 8, 205, 149, 149);
    weather_img = image_at(wc, &weather_dsc[W_CLOUDY], 12, 12);
    lbl_temp = label_box(wc, &tm_b36, COL_TEXT, 12, 52, 125, 40, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(lbl_temp, "--");
    image_at(wc, &sunrise_dsc, 12, 101);
    lbl_sunrise = label_box(wc, &tm_m12, COL_MUTED, 32, 101, 105, 16, LV_TEXT_ALIGN_LEFT);
    image_at(wc, &sunset_dsc, 12, 121);
    lbl_sunset = label_box(wc, &tm_m12, COL_MUTED, 32, 121, 105, 16, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(lbl_sunrise, "восход --:--");
    lv_label_set_text(lbl_sunset, "закат --:--");

    lv_obj_t *tc = card_create(scr, 165, 205, 149, 149);
    lbl_clock = label_box(tc, &tm_b48, COL_TEXT, 0, 37, 149, 48, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(lbl_clock, -1, 0);
    lv_label_set_text(lbl_clock, "--:--");
    lbl_date = label_box(tc, &tm_m16, COL_MUTED, 0, 93, 149, 20, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *pc = card_create(scr, 322, 205, 150, 149);
    lv_obj_t *pz = label_box(pc, &tm_m16, COL_PURPLE, 12, 12, 126, 20, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(pz, "polza.ai");
    lbl_balance = label_box(pc, &tm_b36, COL_TEXT, 12, 56, 126, 40, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(lbl_balance, "--");
    lbl_balance_sub = label_box(pc, &tm_m12, COL_MUTED, 12, 101, 126, 16, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(lbl_balance_sub, "баланс");

    /* ряд 3: задачи */
    task_card_create(&tasks_tracker, scr, 8, "Трекер");
    task_card_create(&tasks_jira, scr, 244, "Jira");
    task_card_render(&tasks_tracker);
    task_card_render(&tasks_jira);

    limit_hint(&lim_block, "Нет данных\nс ПК");
    limit_hint(&lim_week, "Нет данных\nс ПК");
}

void token_ui_set_live(const token_data_t *d)
{
    char buf[32];

    lv_obj_set_x(dot, 354);
    lv_obj_set_style_bg_color(dot, lv_color_hex(COL_GREEN), 0);
    lv_label_set_text(lbl_link, "LIVE");
    lv_obj_set_style_text_color(lbl_link, lv_color_hex(COL_GREEN), 0);

    if (d->usage_ok) {
        limit_set(&lim_block, d->block_pct, d->reset_min);
        limit_set(&lim_week, d->week_pct, d->week_reset_min);
    } else {
        const char *hint = "Нет данных\nс ПК";
        if (!strcmp(d->usage_status, "token_expired")) {
            hint = "Токен истёк:\nзапустите\nclaude";
        } else if (!strcmp(d->usage_status, "no_token")) {
            hint = "Нет входа:\nclaude\n/login";
        } else if (!strcmp(d->usage_status, "unavailable")) {
            hint = "Anthropic\nне отвечает";
        }
        limit_hint(&lim_block, hint);
        limit_hint(&lim_week, hint);
    }
    logo_set_busy(d->busy);

    if (d->weather_ok) {
        lv_image_set_src(weather_img, &weather_dsc[weather_icon(d->weather_code, d->is_day)]);
        snprintf(buf, sizeof(buf), "%.1f°", d->temp_c);
        char *dp = strchr(buf, '.');
        if (dp) *dp = ',';                   /* десятичная запятая: «13,7°» */
        lv_label_set_text(lbl_temp, buf);
        lv_label_set_text_fmt(lbl_sunrise, "восход %s", d->sunrise);
        lv_label_set_text_fmt(lbl_sunset, "закат %s", d->sunset);
    }

    if (d->time[0]) {
        lv_label_set_text(lbl_clock, d->time);
        fmt_date(buf, sizeof(buf), d->date);
        lv_label_set_text(lbl_date, buf);
    }

    if (d->polza_ok) {
        bool low = d->polza_balance < POLZA_LOW_RUB;
        lv_label_set_text_fmt(lbl_balance, "%.0f ₽", d->polza_balance);
        lv_obj_set_style_text_color(lbl_balance, lv_color_hex(low ? COL_RED : COL_TEXT), 0);
        lv_label_set_text(lbl_balance_sub, low ? "мало средств" : "баланс");
        lv_obj_set_style_text_color(lbl_balance_sub, lv_color_hex(low ? COL_RED : COL_MUTED), 0);
    }
}

void token_ui_set_offline(void)
{
    if (!dot) {
        return;
    }
    lv_obj_set_x(dot, 330);
    lv_obj_set_style_bg_color(dot, lv_color_hex(COL_RED), 0);
    lv_label_set_text(lbl_link, "OFFLINE");
    lv_obj_set_style_text_color(lbl_link, lv_color_hex(COL_RED), 0);
    logo_set_busy(false);
}

void token_ui_set_battery(int pct)
{
    if (!lbl_batt) {
        return;
    }
    if (pct < 0) {
        lv_label_set_text(lbl_batt, "--%");
        lv_obj_set_width(batt_fill, 0);
        return;
    }
    if (pct > 100) pct = 100;
    lv_label_set_text_fmt(lbl_batt, "%d%%", pct);
    lv_obj_set_width(batt_fill, 17 * pct / 100);
}

void token_ui_set_tasks(const task_list_t *lists, int count)
{
    bool seen_tracker = false, seen_jira = false;
    for (int i = 0; i < count; i++) {
        task_card_t *c = card_for_label(lists[i].label);
        if (!c) {
            continue;
        }
        if (c == &tasks_tracker) {
            seen_tracker = true;
        } else {
            seen_jira = true;
        }
        /* данные приходят каждые 3 с, перерисовываем только при изменении */
        if (c->has_data && !memcmp(&c->data, &lists[i], sizeof(task_list_t))) {
            continue;
        }
        c->data = lists[i];
        c->has_data = true;
        task_card_render(c);
    }
    task_card_t *cards[] = { &tasks_tracker, &tasks_jira };
    bool seen[] = { seen_tracker, seen_jira };
    for (int i = 0; i < 2; i++) {
        if (!seen[i] && cards[i]->has_data) {  /* интеграцию отключили */
            cards[i]->has_data = false;
            memset(&cards[i]->data, 0, sizeof(task_list_t));
            task_card_render(cards[i]);
        }
    }
}

void token_ui_set_task_delete_cb(task_delete_cb_t cb)
{
    s_del_cb = cb;
}

void token_ui_remove_task(const char *list_id, const char *task_key)
{
    task_card_t *cards[] = { &tasks_tracker, &tasks_jira };
    for (int c = 0; c < 2; c++) {
        task_list_t *t = &cards[c]->data;
        if (!cards[c]->has_data || strcmp(t->id, list_id)) {
            continue;
        }
        for (int i = 0; i < t->count; i++) {
            if (strcmp(t->items[i].key, task_key)) {
                continue;
            }
            memmove(&t->items[i], &t->items[i + 1],
                    sizeof(task_item_t) * (t->count - i - 1));
            t->count--;
            if (t->total > 0) t->total--;
            task_card_render(cards[c]);
            return;
        }
    }
}

static void toast_hide(lv_timer_t *t)
{
    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_delete(t);
}

void token_ui_toast(const char *text, bool success)
{
    if (!toast) {
        toast = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_font(toast, &tm_m16, 0);
        lv_obj_set_style_text_color(toast, lv_color_hex(COL_BG), 0);
        lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(toast, 8, 0);
        lv_obj_set_style_pad_hor(toast, 12, 0);
        lv_obj_set_style_pad_ver(toast, 6, 0);
        lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -130);
    }
    lv_label_set_text(toast, text);
    lv_obj_set_style_bg_color(toast, lv_color_hex(success ? COL_GREEN : COL_RED), 0);
    lv_obj_remove_flag(toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(toast_hide, 2200, NULL);
}
