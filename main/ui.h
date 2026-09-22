#pragma once

#include <stdbool.h>

/* Всё, что показывает экран. Заполняет net.c из ответа magic-qube. */
typedef struct {
    /* лимиты Claude */
    bool  usage_ok;         /* цифры получены от Anthropic и им можно верить */
    char  usage_status[16]; /* ok | token_expired | no_token | unavailable */
    int   block_pct;
    int   reset_min;
    int   week_pct;
    int   week_reset_min;
    bool  busy;             /* Claude Code сейчас работает */

    /* погода и время */
    bool   weather_ok;
    double temp_c;
    int    weather_code;    /* WMO-код open-meteo, -1 если неизвестен */
    bool   is_day;
    char   sunrise[8];
    char   sunset[8];
    char   time[8];
    char   date[24];        /* «Tue Sep 22» с сервера, экран переводит сам */

    /* polza.ai */
    bool   polza_ok;
    double polza_balance;
} token_data_t;

/* Задачи трекеров. */
#define TASK_MAX_LISTS 4
#define TASK_MAX_ITEMS 10

typedef struct {
    char key[16];           /* «TASK-215» */
    char title[104];
} task_item_t;

typedef struct {
    char id[32];            /* id интеграции в magic-qube, нужен для удаления */
    char label[32];         /* «Tracker», «Jira» */
    int  total;             /* всего открытых задач, может быть больше count */
    int  count;
    task_item_t items[TASK_MAX_ITEMS];
} task_list_t;

/* Построить экран. Вызывать под блокировкой LVGL. */
void token_ui_create(void);

/* Свежие данные с сервера. Вызывать под блокировкой LVGL. */
void token_ui_set_live(const token_data_t *d);

/* Связь потеряна: значения остаются последними, в шапке OFFLINE. */
void token_ui_set_offline(void);

/* Заряд батареи в процентах, -1 — неизвестно. */
void token_ui_set_battery(int pct);

/* Списки задач трекеров. Вызывать под блокировкой LVGL. */
void token_ui_set_tasks(const task_list_t *lists, int count);

/* Нажата кнопка «закрыть» у задачи: net.c удаляет её на сервере. */
typedef void (*task_delete_cb_t)(const char *list_id, const char *task_key);
void token_ui_set_task_delete_cb(task_delete_cb_t cb);

/* Сервер подтвердил удаление — убираем строку сразу, не ждём опроса. */
void token_ui_remove_task(const char *list_id, const char *task_key);

/* Короткое сообщение внизу экрана. */
void token_ui_toast(const char *text, bool success);
