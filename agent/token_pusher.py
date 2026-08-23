#!/usr/bin/env python3
"""Token Monitor — отправщик статистики с рабочего ПК на брокер.

Читает логи Claude Code через ccusage и отправляет результат брокеру,
который крутится на домашнем сервере (Raspberry Pi).

Запуск:  python token_pusher.py            (адрес берётся из TM_BROKER)
         python token_pusher.py --broker http://192.168.50.50:8765
"""
import json
import os
import socket
import sys
import time
import urllib.request

import token_agent  # переиспользуем сбор статистики и калибровку

PUSH_EVERY_SEC = 30


def push(broker, payload):
    req = urllib.request.Request(
        broker.rstrip("/") + "/ingest",
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"},
        method="POST")
    with urllib.request.urlopen(req, timeout=10) as r:
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
