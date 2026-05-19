#!/usr/bin/env python3
"""
bridge_auto.py — Claude.ai API bridge for Claude Monitor CYD.

Fetches real utilization from claude.ai (5h + 7d windows) and sends
to CYD display via WiFi (claude-monitor.local) or USB serial fallback.

Usage:
    python3 scripts/bridge_auto.py --session-key sk-ant-sid01-...

Getting your session key:
    1. Open claude.ai in browser, log in
    2. DevTools → Application → Cookies → claude.ai
    3. Copy the value of "sessionKey"
"""

import argparse
import json
import sys
import time
import urllib.request
import urllib.error
import glob
from datetime import datetime, timezone

CLAUDE_AI    = "https://claude.ai/api"
WIFI_HOST    = "claude-monitor.local"
WIFI_TIMEOUT = 3
SERIAL_BAUD  = 115200
UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36"


# ── Claude.ai API ─────────────────────────────────────────────────────────────

def api_get(path: str, session_key: str) -> object:
    req = urllib.request.Request(
        f"{CLAUDE_AI}{path}",
        headers={
            "Cookie":       f"sessionKey={session_key}",
            "User-Agent":   UA,
            "Accept":       "application/json",
            "Referer":      "https://claude.ai/",
        },
    )
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read())


def get_org_id(session_key: str) -> str:
    orgs = api_get("/organizations", session_key)
    if not orgs:
        raise RuntimeError("No organizations returned by API")
    return orgs[0]["id"]


def parse_reset_min(resets_at: str) -> int:
    if not resets_at:
        return 300
    try:
        ts   = resets_at.replace("Z", "+00:00")
        end  = datetime.fromisoformat(ts)
        mins = int((end - datetime.now(timezone.utc)).total_seconds() / 60)
        return max(0, mins)
    except Exception:
        return 300


def build_payload(session_key: str, org_id: str, plan: str) -> dict:
    usage = api_get(f"/organizations/{org_id}/usage", session_key)

    fh = usage.get("five_hour", {})
    sd = usage.get("seven_day", {})

    fh_pct       = min(100, int(fh.get("utilization", 0.0) * 100))
    sd_pct       = min(100, int(sd.get("utilization", 0.0) * 100))
    fh_reset_min = parse_reset_min(fh.get("resets_at", ""))
    sd_reset_min = parse_reset_min(sd.get("resets_at", ""))

    warning = 3 if fh_pct >= 90 else 2 if fh_pct >= 75 else 1 if fh_pct >= 50 else 0

    return {
        "fh_pct":        fh_pct,
        "fh_reset_min":  min(fh_reset_min, 300),
        "sd_pct":        sd_pct,
        "sd_reset_min":  min(sd_reset_min, 10080),
        "warning_level": warning,
        "plan_name":     plan.capitalize(),
    }


# ── Transport helpers ─────────────────────────────────────────────────────────

def wifi_available() -> bool:
    try:
        url = f"http://{WIFI_HOST}/api/status"
        with urllib.request.urlopen(url, timeout=WIFI_TIMEOUT) as r:
            return r.status == 200
    except Exception:
        return False


def send_wifi(payload: dict) -> bool:
    try:
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        req  = urllib.request.Request(
            f"http://{WIFI_HOST}/api/monitor", data=body,
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=WIFI_TIMEOUT) as r:
            return r.status == 200
    except Exception:
        return False


def find_serial_port(hint: str) -> str:
    if hint:
        return hint
    candidates = glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/cu.usbmodem*")
    return candidates[0] if candidates else ""


# ── Main loop ─────────────────────────────────────────────────────────────────

def run_loop(session_key: str, plan: str, port_hint: str, interval: float):
    import serial  # noqa

    mode             = None
    ser              = None
    org_id           = None
    wifi_check_every = 30
    last_wifi_check  = 0

    print("Claude Monitor CYD — Bridge")
    print(f"  Plan:      {plan}")
    print(f"  Interval:  {interval}s")
    print(f"  WiFi:      {WIFI_HOST}")
    print()

    # Fetch org ID once at startup
    print("Fetching org ID from claude.ai... ", end="", flush=True)
    try:
        org_id = get_org_id(session_key)
        print(f"OK ({org_id[:8]}...)")
    except Exception as ex:
        print(f"FAILED: {ex}")
        print("Check your session key (--session-key).")
        sys.exit(1)

    while True:
        now = time.time()

        # Mode detection
        if mode != "wifi" and (now - last_wifi_check > wifi_check_every or mode is None):
            last_wifi_check = now
            if wifi_available():
                if mode != "wifi":
                    if ser:
                        try: ser.close()
                        except Exception: pass
                        ser = None
                    mode = "wifi"
                    print(f"\r[WiFi] Connected to {WIFI_HOST}            ")

        if mode != "wifi":
            if ser is None:
                port = find_serial_port(port_hint)
                if port:
                    try:
                        ser  = serial.Serial(port, SERIAL_BAUD, timeout=1)
                        mode = "serial"
                        print(f"\r[USB]  Connected on {port}            ")
                    except Exception as ex:
                        print(f"\r[USB]  Cannot open {port}: {ex}  ")
                        time.sleep(interval)
                        continue
                else:
                    print(f"\r[WAIT] No CYD found (WiFi or USB)...  ", end="", flush=True)
                    time.sleep(5)
                    continue

        # Build payload
        try:
            payload = build_payload(session_key, org_id, plan)
        except Exception as ex:
            print(f"\r[ERR]  build_payload: {ex}  ", end="", flush=True)
            time.sleep(interval)
            continue

        # Send
        ok = False
        if mode == "wifi":
            ok = send_wifi(payload)
            if not ok:
                mode = None
                last_wifi_check = 0
        elif mode == "serial" and ser:
            try:
                line = json.dumps(payload, separators=(",", ":")) + "\n"
                ser.write(line.encode("utf-8"))
                ok = True
            except Exception:
                ser  = None
                mode = None

        if ok:
            status = ["OK", "CAUTION", "WARNING", "CRITICAL"][payload["warning_level"]]
            tag    = "[WiFi]" if mode == "wifi" else "[USB] "
            print(
                f"\r{tag} {payload['plan_name']}  "
                f"5H:{payload['fh_pct']}%({payload['fh_reset_min']}m)  "
                f"7D:{payload['sd_pct']}%({payload['sd_reset_min']}m)  "
                f"[{status}]  ",
                end="", flush=True,
            )

        time.sleep(interval)


def main():
    parser = argparse.ArgumentParser(description="Claude.ai API bridge for CYD")
    parser.add_argument("--session-key", required=True,
                        help="Claude.ai sessionKey cookie value (sk-ant-sid01-...)")
    parser.add_argument("--plan", default="pro",
                        help="Plan name shown on display (pro, max5, max20, ...)")
    parser.add_argument("--port", default="",
                        help="USB serial port hint (auto-detected if omitted)")
    parser.add_argument("--interval", type=float, default=30.0,
                        help="Polling interval in seconds (default: 30)")
    args = parser.parse_args()

    try:
        import serial  # noqa
    except ImportError:
        print("ERROR: pyserial not installed.")
        sys.exit(1)

    try:
        run_loop(args.session_key, args.plan, args.port, args.interval)
    except KeyboardInterrupt:
        print("\n\nBridge stopped.")


if __name__ == "__main__":
    main()
