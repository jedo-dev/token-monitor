#!/usr/bin/env python3
"""Token Monitor — брокер для домашнего сервера (Raspberry Pi).

Всегда включён. Принимает статистику от ПК (POST /ingest), сам добавляет
часы и погоду и отдаёт всё дисплею (GET /stats). Когда компьютер выключен,
экран продолжает работать на последних известных цифрах.

Запуск:  python3 token_broker.py
Проверка: curl http://localhost:8765/stats
"""
import json
import os
import threading
import time
import urllib.request
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("TM_PORT", "8765"))
LAT = float(os.environ.get("TM_LAT", "55.75"))
LON = float(os.environ.get("TM_LON", "37.62"))
STALE_AFTER_SEC = 300     # после этого данные считаются устаревшими
STATE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "broker_state.json")

_lock = threading.Lock()
_usage = {}               # последнее, что прислал ПК
_usage_ts = 0.0
_sources = {}             # host -> (данные, время) для нескольких машин
_weather = {"ts": 0.0, "data": {}}


def _fetch_weather():
    if time.time() - _weather["ts"] < 900 and _weather["data"]:
        return _weather["data"]
    url = (f"https://api.open-meteo.com/v1/forecast?latitude={LAT}&longitude={LON}"
           "&current=temperature_2m&daily=sunrise,sunset&timezone=auto"
           "&forecast_days=1")
    try:
        with urllib.request.urlopen(url, timeout=10) as r:
            d = json.load(r)
        out = {
            "temp_c": round(float(d["current"]["temperature_2m"]), 1),
            "sunrise": d["daily"]["sunrise"][0][11:16],
            "sunset": d["daily"]["sunset"][0][11:16],
        }
        _weather.update(ts=time.time(), data=out)
        return out
    except Exception as e:
        print(f"[weather] failed: {e}")
        return _weather["data"]


def _merge_sources():
    """Свести данные нескольких машин: проценты — по максимуму
    (лимит общий на аккаунт), токены и стоимость — суммой."""
    fresh = [d for d, ts in _sources.values()
             if time.time() - ts < STALE_AFTER_SEC]
    if not fresh:
        return dict(_usage)
    if len(fresh) == 1:
        return dict(fresh[0])

    merged = dict(fresh[0])
    for key in ("block_pct", "week_pct"):
        merged[key] = max(d.get(key, 0) for d in fresh)
    for key in ("tokens_today",):
        merged[key] = sum(d.get(key, 0) for d in fresh)
    merged["cost_today_usd"] = round(
        sum(float(d.get("cost_today_usd", 0)) for d in fresh), 2)
    merged["busy"] = any(d.get("busy") for d in fresh)
    merged["reset_min"] = min((d.get("reset_min", 0) for d in fresh
                               if d.get("reset_min")), default=0)
    return merged


def _save_state():
    try:
        with open(STATE_FILE, "w", encoding="utf-8") as f:
            json.dump({"usage": _usage, "ts": _usage_ts}, f)
    except OSError as e:
        print(f"[state] save failed: {e}")


def _load_state():
    global _usage, _usage_ts
    try:
        blob = json.load(open(STATE_FILE, encoding="utf-8"))
        _usage, _usage_ts = blob.get("usage", {}), blob.get("ts", 0.0)
        print(f"[state] восстановлено: {len(_usage)} полей")
    except (OSError, ValueError):
        pass


def _build_stats():
    with _lock:
        out = _merge_sources()
        age = time.time() - _usage_ts if _usage_ts else 1e9
    out.update(_fetch_weather())
    local = datetime.now()
    out["time"] = local.strftime("%H:%M")
    out["date"] = local.strftime("%a %d %b")
    out["stale"] = age > STALE_AFTER_SEC
    if out["stale"]:
        out["busy"] = False       # ПК недоступен — точно ничего не считает
    out["ok"] = bool(out.get("block_pct") is not None)
    return out


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path != "/stats":
            self.send_error(404)
            return
        self._send(200, _build_stats())

    def do_POST(self):
        global _usage, _usage_ts
        if self.path != "/ingest":
            self.send_error(404)
            return
        try:
            n = int(self.headers.get("Content-Length", 0))
            data = json.loads(self.rfile.read(n))
        except (ValueError, TypeError):
            self._send(400, {"error": "bad json"})
            return

        host = str(data.pop("host", self.client_address[0]))
        with _lock:
            _sources[host] = (data, time.time())
            _usage = _merge_sources()
            _usage_ts = time.time()
        _save_state()
        print(f"[ingest] {host}: block={data.get('block_pct')}% "
              f"week={data.get('week_pct')}% tokens={data.get('tokens_today')}")
        self._send(200, {"ok": True})

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    _load_state()
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Token Monitor broker: GET http://0.0.0.0:{PORT}/stats, "
          f"POST /ingest")
    srv.serve_forever()
