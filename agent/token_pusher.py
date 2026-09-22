#!/usr/bin/env python3
"""Token Monitor — отправщик статистики с рабочего ПК на брокер.

Читает логи Claude Code через ccusage и отправляет результат брокеру,
который крутится на домашнем сервере (Raspberry Pi).

Запуск:  python token_pusher.py            (адрес берётся из TM_BROKER)
         python token_pusher.py --broker http://192.168.50.50:8765
"""
import json
import re
import os
import socket
import sys
import time
import urllib.request

import token_agent  # переиспользуем сбор статистики и калибровку

PUSH_EVERY_SEC = 30


def api_key():
    """Ключ magic-qube: из TM_API_KEY либо из main/secrets.h, чтобы не
    держать его в двух местах. В лог не пишется."""
    if os.environ.get("TM_API_KEY"):
        return os.environ["TM_API_KEY"].strip()
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "main", "secrets.h")
    try:
        m = re.search(r'#define\s+API_KEY\s+"([^"]+)"',
                      open(path, encoding="utf-8", errors="ignore").read())
        return m.group(1) if m else None
    except OSError:
        return None


def push(broker, payload):
    headers = {"Content-Type": "application/json"}
    key = api_key()
    if key:
        headers["X-API-Key"] = key
    req = urllib.request.Request(
        broker.rstrip("/") + "/display/ingest",
        data=json.dumps(payload).encode(),
        headers=headers,
        method="POST")
    # брокер в домашней сети: мимо VPN-прокси, даже если он задан в окружении
    direct = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with direct.open(req, timeout=10) as r:
        return r.status == 200


def main():
    broker = os.environ.get("TM_BROKER", "http://raspberrypi.local:8765")
    if "--broker" in sys.argv:
        broker = sys.argv[sys.argv.index("--broker") + 1]

    host = socket.gethostname()
    print(f"Отправляю статистику {host} -> {broker} каждые {PUSH_EVERY_SEC}с")

    while True:
        try:
            # погоду и часы добавит брокер — здесь только расход токенов
            data = token_agent._collect(include_weather=False)
            data["host"] = host
            push(broker, data)
            print(f"[push] block={data.get('block_pct')}% "
                  f"week={data.get('week_pct')}% "
                  f"tokens={data.get('tokens_today')}")
        except Exception as e:
            print(f"[push] не получилось: {e}")
        time.sleep(PUSH_EVERY_SEC)


if __name__ == "__main__":
    main()
