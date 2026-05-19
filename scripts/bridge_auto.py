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
import glob
from datetime import datetime, timezone

from curl_cffi import requests as cf_requests

CLAUDE_AI    = "https://claude.ai/api"
WIFI_TIMEOUT = 3
SERIAL_BAUD  = 115200


# ── Claude.ai API ─────────────────────────────────────────────────────────────

class SessionExpiredError(Exception):
    pass


def api_get(path: str, session_key: str) -> object:
    r = cf_requests.get(
        f"{CLAUDE_AI}{path}",
        headers={"Accept": "application/json", "Referer": "https://claude.ai/"},
        cookies={"sessionKey": session_key},
        impersonate="chrome120",
        timeout=10,
    )
    if r.status_code in (401, 403):
        raise SessionExpiredError(f"HTTP {r.status_code} — session key expired")
    r.raise_for_status()
    return r.json()


def get_org_uuid(session_key: str) -> str:
    orgs = api_get("/organizations", session_key)
    if not orgs:
        raise RuntimeError("No organizations returned by API")
    return orgs[0]["uuid"]


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


def build_payload(session_key: str, org_uuid: str, plan: str) -> dict:
    usage = api_get(f"/organizations/{org_uuid}/usage", session_key)

    fh = usage.get("five_hour", {}) or {}
    sd = usage.get("seven_day", {}) or {}

    fh_pct       = min(100, int(fh.get("utilization", 0.0)))
    sd_pct       = min(100, int(sd.get("utilization", 0.0)))
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

def wifi_available(host: str) -> bool:
    try:
        url = f"http://{host}/api/status"
        with urllib.request.urlopen(url, timeout=WIFI_TIMEOUT) as r:
            return r.status == 200
    except Exception:
        return False


def send_wifi(payload: dict, host: str) -> bool:
    try:
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        req  = urllib.request.Request(
            f"http://{host}/api/monitor", data=body,
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

def run_loop(session_key: str, plan: str, port_hint: str, interval: float, wifi_host: str):
    import serial  # noqa

    mode             = None
    ser              = None
    org_uuid           = None
    wifi_check_every = 30
    last_wifi_check  = 0

    print("Claude Monitor CYD — Bridge")
    print(f"  Plan:      {plan}")
    print(f"  Interval:  {interval}s")
    print(f"  WiFi:      {wifi_host}")
    print()

    # Fetch org ID once at startup
    print("Fetching org ID from claude.ai... ", end="", flush=True)
    try:
        org_uuid = get_org_uuid(session_key)
        print(f"OK ({org_uuid[:8]}...)")
    except SessionExpiredError:
        print("FAILED — SESSION KEY EXPIRED")
        print()
        print("  Get a new key: claude.ai → DevTools → Application → Cookies → sessionKey")
        print("  Then update --session-key in ~/Library/LaunchAgents/com.claude.cyd-bridge.plist")
        sys.exit(1)
    except Exception as ex:
        print(f"FAILED: {ex}")
        print("Check your session key (--session-key).")
        sys.exit(1)

    while True:
        now = time.time()

        # Mode detection
        if mode != "wifi" and (now - last_wifi_check > wifi_check_every or mode is None):
            last_wifi_check = now
            if wifi_available(wifi_host):
                if mode != "wifi":
                    if ser:
                        try: ser.close()
                        except Exception: pass
                        ser = None
                    mode = "wifi"
                    print(f"\r[WiFi] Connected to {wifi_host}            ")

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
            payload = build_payload(session_key, org_uuid, plan)
        except SessionExpiredError:
            print("\r[SESSION EXPIRED] Get new sessionKey from claude.ai → update plist → restart bridge  ")
            time.sleep(300)  # check again in 5 min, not every 30s
            continue
        except Exception as ex:
            print(f"\r[ERR]  build_payload: {ex}  ", end="", flush=True)
            time.sleep(interval)
            continue

        # Send
        ok = False
        if mode == "wifi":
            ok = send_wifi(payload, wifi_host)
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
    parser.add_argument("--host", default="claude-monitor.local",
                        help="CYD mDNS hostname (default: claude-monitor.local; "
                             "use claude-monitor-2.local for device #2)")
    args = parser.parse_args()

    try:
        import serial  # noqa
    except ImportError:
        print("ERROR: pyserial not installed.")
        sys.exit(1)

    try:
        run_loop(args.session_key, args.plan, args.port, args.interval, args.host)
    except KeyboardInterrupt:
        print("\n\nBridge stopped.")


if __name__ == "__main__":
    main()
