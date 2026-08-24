#!/usr/bin/env python3
"""Сбор статистики polza.ai (LLM-агрегатор) для дисплея.

Ключ берётся, в порядке приоритета, из:
  1. переменной окружения POLZA_API_KEY (или AI_API_KEY),
  2. файла agent/polza_key.txt,
  3. main/secrets.h проекта (#define AI_API_KEY "...") — чтобы не дублировать.

Ключ никуда, кроме polza.ai, не отправляется и не печатается в лог.
"""
import json
import os
import re
import time
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone

API = "https://polza.ai/api/v1"
_HERE = os.path.dirname(os.path.abspath(__file__))
_cache = {"ts": 0.0, "data": {}}
CACHE_SEC = 60


def _load_key():
    for var in ("POLZA_API_KEY", "AI_API_KEY"):
        if os.environ.get(var):
            return os.environ[var].strip()

    path = os.path.join(_HERE, "polza_key.txt")
    try:
        key = open(path, encoding="utf-8").read().strip()
        if key:
            return key
    except OSError:
        pass

    secrets = os.path.join(_HERE, "..", "main", "secrets.h")
    try:
        src = open(secrets, encoding="utf-8", errors="ignore").read()
        m = re.search(r'#define\s+AI_API_KEY\s+"([^"]+)"', src)
        if m:
            return m.group(1)
    except OSError:
        pass
    return None


def _get(path, key, **params):
    url = f"{API}{path}"
    if params:
        url += "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={
        "Authorization": "Bearer " + key,
        "Accept": "application/json",
    })
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.load(r)


def collect(days=7):
    """Баланс, траты и запросы за сегодня, история по дням."""
    if time.time() - _cache["ts"] < CACHE_SEC and _cache["data"]:
        return _cache["data"]

    key = _load_key()
    if not key:
        return None

    out = {}
    try:
        bal = _get("/balance", key)
        out["balance_rub"] = round(float(bal.get("amount", 0)), 2)
        out["spent_total_rub"] = round(float(bal.get("spentAmount", 0)), 2)
    except Exception as e:
        print(f"[polza] balance failed: {e}")
        return _cache["data"] or None

    # история за последние `days` суток — на график и на счётчики за сегодня
    since = (datetime.now(timezone.utc) - timedelta(days=days)).isoformat()
    items, page = [], 1
    try:
        while page <= 10:
            d = _get("/history/generations", key, limit=100, page=page,
                     dateFrom=since, sortBy="createdAt", sortOrder="desc")
            batch = d.get("items", [])
            items.extend(batch)
            meta = d.get("meta", {})
            if page >= int(meta.get("totalPages", 1)) or not batch:
                break
            page += 1
    except Exception as e:
        print(f"[polza] history failed: {e}")

    today = datetime.now().strftime("%Y-%m-%d")
    per_day, today_cost, today_reqs, today_tokens, errors = {}, 0.0, 0, 0, 0

    for it in items:
        ts = str(it.get("createdAt", ""))
        try:
            local = datetime.fromisoformat(ts.replace("Z", "+00:00")).astimezone()
        except ValueError:
            continue
        day = local.strftime("%Y-%m-%d")
        cost = float(it.get("cost", 0) or 0)
        tokens = int((it.get("usage") or {}).get("total_tokens", 0) or 0)

        slot = per_day.setdefault(day, {"cost": 0.0, "reqs": 0, "tokens": 0})
        slot["cost"] += cost
        slot["reqs"] += 1
        slot["tokens"] += tokens

        if day == today:
            today_cost += cost
            today_reqs += 1
            today_tokens += tokens
        if it.get("status") == "failed":
            errors += 1

    out["spent_today_rub"] = round(today_cost, 2)
    out["requests_today"] = today_reqs
    out["tokens_today"] = today_tokens
    out["requests_total"] = len(items)
    out["errors"] = errors

    history = []
    for i in range(days - 1, -1, -1):
        day = (datetime.now() - timedelta(days=i)).strftime("%Y-%m-%d")
        slot = per_day.get(day, {"cost": 0.0, "reqs": 0})
        history.append({
            "d": day[5:],
            "c": round(slot["cost"], 2),
            "r": slot["reqs"],
        })
    out["history"] = history

    # самая используемая модель за период
    models = {}
    for it in items:
        name = str(it.get("modelDisplayName") or it.get("model") or "")
        models[name] = models.get(name, 0) + float(it.get("cost", 0) or 0)
    if models:
        top = max(models.items(), key=lambda kv: kv[1])
        out["top_model"] = top[0].split("/")[-1][:22]

    _cache.update(ts=time.time(), data=out)
    return out


if __name__ == "__main__":
    import sys
    sys.stdout.reconfigure(encoding="utf-8")
    d = collect()
    print(json.dumps(d, ensure_ascii=False, indent=2) if d
          else "Ключ не найден: задайте POLZA_API_KEY или agent/polza_key.txt")
