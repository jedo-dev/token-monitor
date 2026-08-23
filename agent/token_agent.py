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
import urllib.request
from datetime import datetime, timezone, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = 8765
REFRESH_SEC = 30          # как часто перечитывать статистику ccusage
CALIB_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "calibration.json")
# Лимит недели в долларах — подстройте под свой тариф (используется
# только для полосы "Week %" внизу экрана).
WEEKLY_COST_LIMIT_USD = float(os.environ.get("TM_WEEK_LIMIT_USD", "140"))

# Координаты для погоды и рассвета/заката (по умолчанию Москва).
LAT = float(os.environ.get("TM_LAT", "55.75"))
LON = float(os.environ.get("TM_LON", "37.62"))

# Считаем ассистента "работающим", если в логах есть запись за последние N секунд
BUSY_WINDOW_SEC = 90

_lock = threading.Lock()
_stats = {
    "block_pct": 0,
    "tokens_today": 0,
    "cost_today_usd": 0.0,
    "reset_min": 0,
    "week_pct": 0,
    "week_reset_min": 0,
    "temp_c": 0.0,
    "sunrise": "--:--",
    "sunset": "--:--",
    "busy": False,
    "ok": False,
}
_weather = {"ts": 0.0, "data": {}}


def _fetch_weather():
    """Температура и рассвет/закат с open-meteo (без ключа), кэш на 15 минут."""
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


def _claude_busy():
    """Есть ли активность Claude Code прямо сейчас — по времени изменения
    файлов сессий в ~/.claude/projects."""
    root = os.path.expanduser("~/.claude/projects")
    newest = 0.0
    try:
        for dirpath, _dirs, files in os.walk(root):
            for name in files:
                if name.endswith(".jsonl"):
                    try:
                        newest = max(newest, os.path.getmtime(
                            os.path.join(dirpath, name)))
                    except OSError:
                        pass
    except OSError:
        return False
    return (time.time() - newest) < BUSY_WINDOW_SEC


def _load_calibration():
    try:
        return json.load(open(CALIB_FILE, encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def _active_block_tokens():
    """Токены текущего 5-часового блока по данным ccusage."""
    blocks = _run_ccusage(["blocks"]) or {}
    active = next((b for b in blocks.get("blocks", []) if b.get("isActive")), None)
    return int(active.get("totalTokens", 0)) if active else 0


def _week_tokens():
    daily = _run_ccusage(["daily"]) or {}
    week_ago = (datetime.now() - timedelta(days=7)).strftime("%Y-%m-%d")
    return int(sum(d.get("totalTokens", 0) for d in daily.get("daily", [])
                   if d.get("period", "") >= week_ago))


def calibrate(kind, observed_pct):
    """Вычислить реальный лимит по проценту, который показывает Claude.
    kind: 'block' или 'week'."""
    if not 0 < observed_pct <= 100:
        print("Процент должен быть в диапазоне 1..100")
        return
    tokens = _active_block_tokens() if kind == "block" else _week_tokens()
    if tokens <= 0:
        print("Нет данных ccusage для калибровки "
              "(нет активного блока или пустая статистика)")
        return
    limit = int(tokens / (observed_pct / 100.0))
    calib = _load_calibration()
    calib[f"{kind}_token_limit"] = limit
    with open(CALIB_FILE, "w", encoding="utf-8") as f:
        json.dump(calib, f, indent=2)
    print(f"Откалибровано: {kind} = {tokens:,} токенов при {observed_pct}% "
          f"→ лимит {limit:,} токенов. Сохранено в {CALIB_FILE}")


def _read_windows_credential(target):
    """Прочитать generic-credential из Windows Credential Manager.
    Токен не логируется и не покидает этот процесс."""
    import ctypes
    import ctypes.wintypes as wt

    class CREDENTIAL(ctypes.Structure):
        _fields_ = [
            ("Flags", wt.DWORD), ("Type", wt.DWORD),
            ("TargetName", wt.LPWSTR), ("Comment", wt.LPWSTR),
            ("LastWritten", wt.FILETIME),
            ("CredentialBlobSize", wt.DWORD),
            ("CredentialBlob", ctypes.POINTER(ctypes.c_byte)),
            ("Persist", wt.DWORD), ("AttributeCount", wt.DWORD),
            ("Attributes", ctypes.c_void_p),
            ("TargetAlias", wt.LPWSTR), ("UserName", wt.LPWSTR),
        ]

    adv = ctypes.windll.advapi32
    pcred = ctypes.POINTER(CREDENTIAL)()
    if not adv.CredReadW(target, 1, 0, ctypes.byref(pcred)):  # 1 = GENERIC
        return None
    try:
        raw = ctypes.string_at(pcred.contents.CredentialBlob,
                               pcred.contents.CredentialBlobSize)
    finally:
        adv.CredFree(pcred)
    for enc in ("utf-8", "utf-16-le"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return None


def _get_claude_token():
    """Найти OAuth-токен Claude Code: файл .token рядом с агентом
    (созданный через `claude setup-token`), затем .credentials.json,
    затем Windows Credential Manager."""
    tok_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".token")
    try:
        tok = open(tok_file, encoding="utf-8").read().strip()
        if tok:
            return tok
    except OSError:
        pass

    path = os.path.expanduser("~/.claude/.credentials.json")
    try:
        blob = json.load(open(path, encoding="utf-8"))
        tok = blob.get("claudeAiOauth", {}).get("accessToken")
        if tok:
            return tok
    except (OSError, ValueError):
        pass

    if os.name == "nt":
        for target in ("Claude Code-credentials", "Claude Code", "claude"):
            try:
                blob = _read_windows_credential(target)
            except Exception:
                blob = None
            if not blob:
                continue
            try:
                tok = json.loads(blob).get("claudeAiOauth", {}).get("accessToken")
                if tok:
                    return tok
            except ValueError:
                continue
    return None


def _pct(v):
    """Utilization может прийти как 0..1 или 0..100 — нормализуем в %."""
    if v is None:
        return None
    v = float(v)
    if 0 < v <= 1.0 and v != int(v):
        v *= 100
    return min(100, round(v))


def _fetch_api_usage():
    """Точные квоты с официального эндпоинта Anthropic (как в /usage
    самого Claude Code). Возвращает dict или None при любой проблеме."""
    tok = _get_claude_token()
    # эндпоинт принимает только OAuth-токены подписки; с ключами вида
    # sk-ant-api... он всё равно ответит 401, поэтому не тратим время
    if not tok or not tok.startswith("sk-ant-oat"):
        return None
    req = urllib.request.Request(
        "https://api.anthropic.com/api/oauth/usage",
        headers={
            "Authorization": "Bearer " + tok,
            "anthropic-beta": "oauth-2025-04-20",
            "Content-Type": "application/json",
        })
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            d = json.load(r)
    except Exception as e:
        print(f"[api] usage endpoint failed: {e}")
        return None

    out = {}
    five = d.get("five_hour") or {}
    week = d.get("seven_day") or {}
    p = _pct(five.get("utilization"))
    if p is not None:
        out["block_pct"] = p
    if five.get("resets_at"):
        try:
            end = _parse_iso(five["resets_at"])
            out["reset_min"] = max(0, int(
                (end - datetime.now(timezone.utc)).total_seconds() // 60))
        except ValueError:
            pass
    p = _pct(week.get("utilization"))
    if p is not None:
        out["week_pct"] = p
    print(f"[api] five_hour={out.get('block_pct')}% "
          f"seven_day={out.get('week_pct')}% reset_min={out.get('reset_min')}")
    return out or None


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

    # --- точные квоты из API Anthropic (как в /usage Claude Code) ---
    api = _fetch_api_usage() or {}
    out.update(api)

    # --- 5-часовой блок (эвристика ccusage — только если API недоступен) ---
    blocks = _run_ccusage(["blocks"]) or {}
    block_list = blocks.get("blocks", [])
    active = next((b for b in block_list if b.get("isActive")), None)
    # лимит = максимум токенов среди завершённых блоков (та же эвристика,
    # что у ccusage blocks --token-limit max)
    done_tokens = [b.get("totalTokens", 0) for b in block_list
                   if not b.get("isActive") and not b.get("isGap")]
    limit = max(done_tokens) if done_tokens else 0
    calib = _load_calibration()
    if "block_pct" not in out:
        if active:
            tok = active.get("totalTokens", 0)
            # откалиброванный лимит точнее эвристики "максимум за историю"
            block_limit = calib.get("block_token_limit") or limit
            out["block_pct"] = (min(100, round(tok * 100 / block_limit))
                                if block_limit else 0)
        else:
            out["block_pct"] = 0
    if "reset_min" not in out:
        if active:
            try:
                end = _parse_iso(active["endTime"])
                out["reset_min"] = max(0, int((end - now).total_seconds() // 60))
            except (KeyError, ValueError):
                out["reset_min"] = 0
        else:
            out["reset_min"] = 0

    # --- за день и за неделю ---
    daily = _run_ccusage(["daily"]) or {}
    days = daily.get("daily", [])
    today_str = datetime.now().strftime("%Y-%m-%d")
    today = next((d for d in days if d.get("period") == today_str), None)
    out["tokens_today"] = int(today.get("totalTokens", 0)) if today else 0
    out["cost_today_usd"] = round(float(today.get("totalCost", 0.0)), 2) if today else 0.0

    if "week_pct" not in out:
        week_ago = datetime.now() - timedelta(days=7)
        recent = [d for d in days if d.get("period", "") >= week_ago.strftime("%Y-%m-%d")]
        week_limit = calib.get("week_token_limit")
        if week_limit:
            week_tokens = sum(d.get("totalTokens", 0) for d in recent)
            out["week_pct"] = min(100, round(week_tokens * 100 / week_limit))
        else:
            week_cost = sum(float(d.get("totalCost", 0.0)) for d in recent)
            out["week_pct"] = min(100, round(week_cost * 100 / WEEKLY_COST_LIMIT_USD))

    # недельный сброс: по дню/времени из calibration.json, иначе скользящие 7 дней
    if "week_reset_min" not in out:
        wd = calib.get("week_reset_weekday")   # 0=понедельник ... 6=воскресенье
        wh = calib.get("week_reset_hour", 0)
        if wd is not None:
            local = datetime.now()
            days_ahead = (wd - local.weekday()) % 7
            nxt = (local + timedelta(days=days_ahead)).replace(
                hour=int(wh), minute=0, second=0, microsecond=0)
            if nxt <= local:
                nxt += timedelta(days=7)
            out["week_reset_min"] = int((nxt - local).total_seconds() // 60)
        else:
            out["week_reset_min"] = 0

    out.update(_fetch_weather())
    out["busy"] = _claude_busy()
    local = datetime.now()
    out["time"] = local.strftime("%H:%M")
    out["date"] = local.strftime("%a %d %b")
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
    import sys

    # Калибровка: python token_agent.py --calibrate 22 [--week 41]
    if "--calibrate" in sys.argv:
        i = sys.argv.index("--calibrate")
        calibrate("block", float(sys.argv[i + 1]))
        if "--week" in sys.argv:
            j = sys.argv.index("--week")
            calibrate("week", float(sys.argv[j + 1]))
        raise SystemExit

    threading.Thread(target=_refresher, daemon=True).start()
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Token Monitor agent: http://0.0.0.0:{PORT}/stats "
          f"(обновление каждые {REFRESH_SEC}с)")
    srv.serve_forever()
