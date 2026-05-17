#!/usr/bin/env python3
"""
bridge_auto.py — Auto-detecting bridge for Claude Monitor CYD.

Tries WiFi (claude-monitor.local) first, falls back to USB serial.

Usage:
    python3 scripts/bridge_auto.py --plan pro
"""

import argparse
import json
import sys
import time
import urllib.request
import urllib.error
import glob
from datetime import datetime, timezone

from claude_monitor.data.analysis import analyze_usage
from claude_monitor.core.plans import Plans, get_token_limit, get_cost_limit


WIFI_HOST    = "claude-monitor.local"
WIFI_TIMEOUT = 3
SERIAL_BAUD  = 115200


# ── Payload builder ───────────────────────────────────────────────────────────

def build_payload(plan: str) -> dict:
    data   = analyze_usage(hours_back=192, quick_start=False, use_cache=False)
    blocks = data.get("blocks", [])
    token_limit = get_token_limit(plan, blocks)
    cost_limit  = get_cost_limit(plan)
    msg_limit   = Plans.get_message_limit(plan)

    active_block = next((b for b in blocks if b.get("isActive", False)), None)

    if active_block:
        total_tokens = active_block.get("totalTokens", 0)
        cost_used    = active_block.get("costUSD", 0.0)
        messages     = active_block.get("sentMessagesCount", 0)
        duration     = active_block.get("durationMinutes", 1) or 1
        tc           = active_block.get("tokenCounts", {})
        io_tokens    = tc.get("inputTokens", 0) + tc.get("outputTokens", 0)
        burn_rate    = io_tokens / duration

        try:
            end_time  = datetime.fromisoformat(str(active_block.get("endTime", "")))
            reset_min = max(0, int((end_time - datetime.now(timezone.utc)).total_seconds() / 60))
        except (ValueError, TypeError):
            reset_min = 300

        per_model   = active_block.get("perModelStats", {})
        short_model, model_pct = "Sonnet", 0
        if per_model:
            total_entries = sum(s.get("entries_count", 0) for s in per_model.values())
            for m, stats in per_model.items():
                p = int(stats.get("entries_count", 0) * 100 / max(total_entries, 1))
                if p > model_pct:
                    model_pct = p
                    ml = m.lower()
                    short_model = ("Opus" if "opus" in ml
                                   else "Haiku" if "haiku" in ml else "Sonnet")

        projection   = active_block.get("projection", {})
        if projection and "remainingMinutes" in projection:
            depletion_min = int(projection["remainingMinutes"])
        elif burn_rate > 0:
            depletion_min = int(max(0, token_limit - total_tokens) / burn_rate)
        else:
            depletion_min = 999
    else:
        total_tokens = cost_used = messages = burn_rate = 0
        reset_min, depletion_min, model_pct = 300, 999, 0
        short_model = "--"
        duration    = 1

    pct     = total_tokens / token_limit if token_limit > 0 else 0
    warning = 3 if pct >= 0.90 else 2 if pct >= 0.75 else 1 if pct >= 0.50 else 0
    plan_cfg = Plans.get_plan_by_name(plan) or Plans.get_plan_by_name("custom")

    return {
        "tokens_used":         total_tokens,
        "tokens_limit":        token_limit,
        "cost_used":           round(cost_used, 2),
        "cost_limit":          round(cost_limit, 2),
        "msgs_used":           messages,
        "msgs_limit":          msg_limit,
        "burn_rate":           round(burn_rate, 1),
        "cost_rate":           round(cost_used / duration, 4),
        "depletion_min":       min(int(depletion_min), 999),
        "reset_min":           min(reset_min, 300),
        "session_elapsed_min": min(300 - reset_min, 300),
        "session_total_min":   300,
        "plan_name":           plan_cfg.display_name,
        "model":               short_model,
        "model_pct":           model_pct,
        "warning_level":       warning,
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

def run_loop(plan: str, port_hint: str, interval: float):
    import serial

    mode           = None
    ser            = None
    wifi_check_every = 30
    last_wifi_check  = 0

    print(f"Claude Monitor CYD — Auto Bridge")
    print(f"  Plan:      {plan}")
    print(f"  Interval:  {interval}s")
    print(f"  WiFi:      {WIFI_HOST}")
    print()

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
            payload = build_payload(plan)
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
            pct    = int(payload["tokens_used"] * 100 / max(payload["tokens_limit"], 1))
            status = ["OK", "CAUTION", "WARNING", "CRITICAL"][payload["warning_level"]]
            tag    = "[WiFi]" if mode == "wifi" else "[USB] "
            print(
                f"\r{tag} {payload['plan_name']}  "
                f"Tok:{payload['tokens_used']:>6}/{payload['tokens_limit']}({pct}%)  "
                f"${payload['cost_used']:.2f}  "
                f"Msg:{payload['msgs_used']}/{payload['msgs_limit']}  "
                f"{payload['model']} {payload['model_pct']}%  [{status}]  ",
                end="", flush=True,
            )

        time.sleep(interval)


def main():
    parser = argparse.ArgumentParser(description="Auto bridge: WiFi first, USB fallback")
    parser.add_argument("--plan", choices=["pro", "max5", "max20", "custom"],
                        default="pro")
    parser.add_argument("--port", default="",
                        help="USB serial port hint (auto-detected if omitted)")
    parser.add_argument("--interval", type=float, default=30.0)
    args = parser.parse_args()

    try:
        import serial  # noqa
    except ImportError:
        print("ERROR: pyserial not installed.")
        sys.exit(1)

    try:
        run_loop(args.plan, args.port, args.interval)
    except KeyboardInterrupt:
        print("\n\nBridge stopped.")


if __name__ == "__main__":
    main()
