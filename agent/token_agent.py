#!/usr/bin/env python3
"""Token Monitor — локальный брокер v1.

Читает статистику Claude Code через ccusage (npx) и отдаёт компактный JSON
для дисплея ESP32 по HTTP в локальной сети. Никакие данные наружу не уходят.

Запуск:  python token_agent.py
Проверка: curl http://localhost:8765/stats
"""
import json
import os
import subprocess
import threading
import time
from datetime import datetime, timezone, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = 8765
REFRESH_SEC = 30          # как часто перечитывать статистику ccusage
# Лимит недели в долларах — подстройте под свой тариф (используется
# только для полосы "Week %" внизу экрана).
WEEKLY_COST_LIMIT_USD = float(os.environ.get("TM_WEEK_LIMIT_USD", "140"))

_lock = threading.Lock()
_stats = {
    "block_pct": 0,
    "tokens_today": 0,
    "cost_today_usd": 0.0,
    "reset_min": 0,
    "week_pct": 0,
    "ok": False,
}


def _run_ccusage(args):
    """Запустить ccusage через npx, вернуть распарсенный JSON или None."""
    cmd = ["npx", "-y", "ccusage@latest"] + args + ["--json"]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=120, shell=(os.name == "nt"))
        if r.returncode != 0:
            print(f"[ccusage] {' '.join(args)} rc={r.returncode}: {r.stderr[:200]}")
            return None
        return json.loads(r.stdout)
    except Exception as e:
        print(f"[ccusage] {' '.join(args)} failed: {e}")
        return None


def _parse_iso(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00"))


def _collect():
    now = datetime.now(timezone.utc)
    out = {}

    # --- 5-часовой блок ---
    blocks = _run_ccusage(["blocks"]) or {}
    block_list = blocks.get("blocks", [])
    active = next((b for b in block_list if b.get("isActive")), None)
    # лимит = максимум токенов среди завершённых блоков (та же эвристика,
    # что у ccusage blocks --token-limit max)
    done_tokens = [b.get("totalTokens", 0) for b in block_list
                   if not b.get("isActive") and not b.get("isGap")]
    limit = max(done_tokens) if done_tokens else 0
    if active:
        tok = active.get("totalTokens", 0)
        out["block_pct"] = min(100, round(tok * 100 / limit)) if limit else 0
        try:
            end = _parse_iso(active["endTime"])
            out["reset_min"] = max(0, int((end - now).total_seconds() // 60))
        except (KeyError, ValueError):
            out["reset_min"] = 0
    else:
        out["block_pct"] = 0
        out["reset_min"] = 0

    # --- за день и за неделю ---
    daily = _run_ccusage(["daily"]) or {}
    days = daily.get("daily", [])
    today_str = datetime.now().strftime("%Y-%m-%d")
    today = next((d for d in days if d.get("period") == today_str), None)
    out["tokens_today"] = int(today.get("totalTokens", 0)) if today else 0
    out["cost_today_usd"] = round(float(today.get("totalCost", 0.0)), 2) if today else 0.0

    week_ago = datetime.now() - timedelta(days=7)
    week_cost = sum(float(d.get("totalCost", 0.0)) for d in days
                    if d.get("period", "") >= week_ago.strftime("%Y-%m-%d"))
    out["week_pct"] = min(100, round(week_cost * 100 / WEEKLY_COST_LIMIT_USD))

    out["ok"] = bool(block_list or days)
    return out


def _refresher():
    global _stats
    while True:
        try:
            fresh = _collect()
            with _lock:
                _stats = fresh
            print(f"[stats] {fresh}")
        except Exception as e:
            print(f"[refresh] error: {e}")
        time.sleep(REFRESH_SEC)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/stats":
            self.send_error(404)
            return
        with _lock:
            body = json.dumps(_stats).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        pass  # не засорять консоль каждым опросом дисплея


if __name__ == "__main__":
    threading.Thread(target=_refresher, daemon=True).start()
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Token Monitor agent: http://0.0.0.0:{PORT}/stats "
          f"(обновление каждые {REFRESH_SEC}с)")
    srv.serve_forever()
