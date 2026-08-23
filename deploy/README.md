# Разворачивание: брокер на Raspberry Pi + отправщик на ПК

Схема: Raspberry всегда включена и отвечает дисплею, компьютер лишь присылает
ей расход токенов, пока работает.

```
ПК (Claude Code) --POST /ingest--> Raspberry Pi (брокер) <--GET /stats-- дисплей
```

## 1. Raspberry Pi

Скопировать агент и поднять службу (замените `pi` и адрес на свои):

```bash
sudo mkdir -p /opt/token-monitor
sudo chown -R pi:pi /opt/token-monitor
scp -r agent pi@raspberrypi.local:/opt/token-monitor/
scp deploy/token-broker.service pi@raspberrypi.local:/tmp/
```

На самой Raspberry:

```bash
sudo mv /tmp/token-broker.service /etc/systemd/system/
sudo systemctl enable --now token-broker
systemctl status token-broker
```

Проверка (с любого компьютера в сети):

```bash
curl http://raspberrypi.local:8765/stats
```

Координаты для погоды задаются в `token-broker.service` (`TM_LAT`, `TM_LON`).
Ничего, кроме Python 3, на Raspberry не нужно: ccusage там не запускается.

## 2. Компьютер с Claude Code

Разовая настройка автозапуска (укажите адрес своей Raspberry):

```powershell
powershell -ExecutionPolicy Bypass -File deploy\install-pusher-autostart.ps1 -Broker http://192.168.50.50:8765
Start-ScheduledTask -TaskName TokenMonitorPusher
```

Проверить вручную:

```bash
cd agent && python token_pusher.py --broker http://192.168.50.50:8765
```

Калибровка процентов делается на ПК, как и раньше:

```bash
python token_agent.py --calibrate 22 --week 41
```

## 3. Дисплей

В `main/secrets.h` заменить адрес на Raspberry и перепрошить:

```c
#define AGENT_URL  "http://192.168.50.50:8765/stats"
```

## Что происходит, когда ПК выключен

Брокер продолжает отдавать последние полученные цифры (они переживают и его
перезапуск — сохраняются в `broker_state.json`), а часы и погода обновляются
как обычно. В ответе появляется `"stale": true`. Дисплей остаётся на связи.

## Несколько компьютеров

Каждый шлёт свои данные со своим `host`. Брокер складывает токены и стоимость,
а проценты берёт по максимуму — лимит-то общий на аккаунт.
