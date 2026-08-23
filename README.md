# Token Monitor

Настольный дисплей на базе **Waveshare ESP32-S3-Touch-LCD-4 (Rev 4.0, 480×480)** для
отслеживания использования токенов ИИ-ассистентов (Claude Code).

Вдохновлён проектом Token Monitor:
https://cnx-software.ru/2026/08/10/token-monitor-nastolnyj-displej-na-baze-esp32-s3-dlya-otslezhivaniya-ispolzovaniya-ii-assistentov-dlya-programmirovaniya-kraudfanding/

## Стек

- ESP-IDF v5.5 + LVGL 9
- BSP-компонент `waveshare/esp32_s3_touch_lcd_4` (драйверы ST7701, GT911, подсветка)

## Сборка и прошивка

```bash
idf.py build
idf.py -p COM7 flash monitor
```

## Статус

- [x] Каркас проекта, UI с демо-данными (шкала 5h-блока, токены/стоимость за день, недельный лимит)
- [ ] Wi-Fi + получение реальных данных со скрипта на ПК (ccusage)
- [ ] Настройки, ночной режим, тач-жесты
