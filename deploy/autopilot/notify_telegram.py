#!/usr/bin/env python3
"""One-way ECU Platform autopilot notifications through Telegram Bot API."""

from __future__ import annotations

import os
import sys
import urllib.parse
import urllib.request
from pathlib import Path

DEFAULT_ENV = Path.home() / ".config" / "ecu-platform" / "autopilot.env"

ICONS = {
    "STARTED": "🚀",
    "CHECKPOINT": "🔎",
    "COMPLETE": "✅",
    "BLOCKED": "🚨",
    "ROLLBACK": "↩️",
    "INFO": "ℹ️",
}


def load_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        values[key.strip()] = value
    return values


def main(argv: list[str]) -> int:
    if len(argv) < 4:
        print("usage: notify_telegram.py <EVENT> <TASK> <MESSAGE>", file=sys.stderr)
        return 2

    event = argv[1].upper()
    task = argv[2]
    message = " ".join(argv[3:]).strip()
    env_path = Path(os.environ.get("ECU_AUTOPILOT_ENV", str(DEFAULT_ENV)))

    if not env_path.is_file():
        print("autopilot notifier: private env file missing", file=sys.stderr)
        return 3

    env = load_env(env_path)
    token = env.get("TELEGRAM_BOT_TOKEN", "").strip()
    chat_id = env.get("ECU_AUTOPILOT_TELEGRAM_CHAT_ID", "").strip()
    if not token or not chat_id:
        print("autopilot notifier: Telegram secret or chat id missing", file=sys.stderr)
        return 4

    icon = ICONS.get(event, ICONS["INFO"])
    title = f"{icon} ECU Platform — {task} {event}"
    text = title if not message else f"{title}\n{message}"

    payload = urllib.parse.urlencode(
        {
            "chat_id": chat_id,
            "text": text,
            "disable_web_page_preview": "true",
        }
    ).encode("utf-8")

    url = f"https://api.telegram.org/bot{token}/sendMessage"
    request = urllib.request.Request(url, data=payload, method="POST")
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            if not 200 <= response.status < 300:
                print("autopilot notifier: Telegram returned non-success status", file=sys.stderr)
                return 5
    except Exception:
        print("autopilot notifier: Telegram delivery failed", file=sys.stderr)
        return 6
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
