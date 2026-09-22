"""Точные лимиты подписки Claude — те же цифры, что показывает /usage.

Источник — эндпоинт api.anthropic.com/api/oauth/usage. Это обычный запрос
на чтение: токены не тратятся и новый 5-часовой блок не открывается.
Нужен OAuth-токен подписки, который появляется после `claude /login`
(лежит в ~/.claude/.credentials.json).

Когда опрашиваем:
  * после вашей активности в Claude Code — не чаще раза в минуту;
  * в простое — раз в 10 минут: лимиты общие с claude.ai и телефоном,
    а там активность по локальным журналам не видна.

Токен сам не обновляем: это делает Claude Code при запуске. Если токен
протух, отдаём usage_status = "token_expired", экран покажет подсказку.
"""
import glob
import json
import os
import socket
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone

USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
CREDENTIALS = os.path.expanduser("~/.claude/.credentials.json")
PROJECTS = os.path.expanduser("~/.claude/projects")
CACHE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "limits_cache.json")

ACTIVE_POLL_SEC = 60      # после активности — не чаще раза в минуту
IDLE_POLL_SEC = 600       # в простое — раз в 10 минут
RATE_LIMIT_BACKOFF = 300  # после 429 эндпоинт просим не чаще раза в 5 минут

# Anthropic не обслуживает российские IP, поэтому идём через локальный
# VPN-клиент. Если он в режиме TUN, порт закрыт — тогда просто напрямую.
PROXY = os.environ.get("TM_PROXY", "http://127.0.0.1:10809")

_state = {"last_poll": 0.0, "backoff_until": 0.0}


def _read_token():
    """(access_token, expires_at_sec) или (None, None)."""
    try:
        oauth = json.load(open(CREDENTIALS, encoding="utf-8")).get("claudeAiOauth")
    except (OSError, ValueError):
        return None, None
    if not oauth or not oauth.get("accessToken"):
        return None, None
    exp = oauth.get("expiresAt")
    return oauth["accessToken"], (exp / 1000 if exp else None)


def _last_activity() -> float:
    """Время последней записи в журналы Claude Code (CLI и приложение)."""
    newest = 0.0
    for path in glob.glob(os.path.join(PROJECTS, "**", "*.jsonl"), recursive=True):
        try:
            newest = max(newest, os.path.getmtime(path))
        except OSError:
            pass
    return newest


def _opener():
    """Через прокси, если VPN-клиент слушает порт; иначе напрямую (TUN)."""
    host, _, port = PROXY.rpartition("//")[2].partition(":")
    try:
        with socket.create_connection((host, int(port or 80)), timeout=0.3):
            return urllib.request.build_opener(
                urllib.request.ProxyHandler({"https": PROXY, "http": PROXY}))
    except OSError:
        return urllib.request.build_opener(urllib.request.ProxyHandler({}))


def _minutes_until(iso):
    if not iso:
        return 0
    try:
        end = datetime.fromisoformat(iso.replace("Z", "+00:00"))
    except ValueError:
        return 0
    return max(0, int((end - datetime.now(timezone.utc)).total_seconds() // 60))


def _load_cache():
    try:
        return json.load(open(CACHE_FILE, encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def _save_cache(data):
    try:
        with open(CACHE_FILE, "w", encoding="utf-8") as f:
            json.dump(data, f)
    except OSError as e:
        print(f"[limits] cache save failed: {e}")


def _poll(token):
    req = urllib.request.Request(USAGE_URL, headers={
        "Authorization": "Bearer " + token,
        "anthropic-beta": "oauth-2025-04-20",
        "Content-Type": "application/json",
    })
    with _opener().open(req, timeout=15) as r:
        d = json.load(r)
    five = d.get("five_hour") or {}
    week = d.get("seven_day") or {}
    return {
        "five_pct": five.get("utilization"),
        "five_reset": five.get("resets_at"),
        "week_pct": week.get("utilization"),
        "week_reset": week.get("resets_at"),
        "polled_at": time.time(),
    }


def _should_poll(cache, now):
    if not cache.get("polled_at"):
        return True
    since = now - _state["last_poll"] if _state["last_poll"] else now - cache["polled_at"]
    if _last_activity() > cache["polled_at"] and since >= ACTIVE_POLL_SEC:
        return True
    return since >= IDLE_POLL_SEC


def get():
    """Поля для экрана: block_pct, reset_min, week_pct, week_reset_min
    и usage_status: ok | token_expired | no_token | unavailable."""
    now = time.time()
    token, expires = _read_token()
    if not token:
        return {"usage_status": "no_token"}
    if expires and expires < now:
        return {"usage_status": "token_expired"}

    cache = _load_cache()
    if now >= _state["backoff_until"] and _should_poll(cache, now):
        _state["last_poll"] = now
        try:
            cache = _poll(token)
            _save_cache(cache)
            print(f"[limits] 5h={cache['five_pct']}% 7d={cache['week_pct']}%")
        except urllib.error.HTTPError as e:
            if e.code in (401, 403):
                return {"usage_status": "token_expired"}
            if e.code == 429:
                _state["backoff_until"] = now + RATE_LIMIT_BACKOFF
            print(f"[limits] HTTP {e.code}")
        except (OSError, ValueError) as e:
            print(f"[limits] poll failed: {e}")

    if not cache.get("polled_at"):
        return {"usage_status": "unavailable"}

    # Минуты до сброса считаем на лету от сохранённого времени. Если окно
    # уже сбросилось, а нового запроса ещё не было — там честно ноль.
    out = {"usage_status": "ok"}
    for key, pct_key, reset_key in (("block", "five_pct", "five_reset"),
                                    ("week", "week_pct", "week_reset")):
        left = _minutes_until(cache.get(reset_key))
        pct = cache.get(pct_key)
        out[f"{key}_pct" if key == "week" else "block_pct"] = (
            round(pct) if pct is not None and left > 0 else 0)
        out["reset_min" if key == "block" else "week_reset_min"] = left
    return out


if __name__ == "__main__":
    import sys
    sys.stdout.reconfigure(encoding="utf-8")
    print(json.dumps(get(), ensure_ascii=False))
