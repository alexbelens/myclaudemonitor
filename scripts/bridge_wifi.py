#!/usr/bin/env python3
"""
bridge_wifi.py — WiFi bridge for Claude Monitor CYD.

Sends monitor data via HTTP POST to claude-monitor.local (or custom host/IP).
The CYD must be flashed with firmware v3 and have WiFi configured via setup_wifi.py.

Usage:
    python3 scripts/bridge_wifi.py --plan pro
    python3 scripts/bridge_wifi.py --plan pro --host 192.168.1.42
    python3 scripts/bridge_wifi.py --plan pro --interval 30
"""

import argparse
import json
import sys
import time
import urllib.request
import urllib.error
from datetime import datetime, timezone

from claude_monitor.data.analysis import analyze_usage
from claude_monitor.core.plans import Plans, get_token_limit, get_cost_limit


def build_payload(plan: str) -> dict:
    data = analyze_usage(hours_back=192, quick_start=False, use_cache=False)
    blocks = data.get("blocks", [])
    token_limit = get_token_limit(plan, blocks)
    cost_limit = get_cost_limit(plan)
    msg_limit = Plans.get_message_limit(plan)

    active_block = next((b for b in blocks if b.get("isActive", False)), None)

    if active_block:
        total_tokens = active_block.get("totalTokens", 0)
        cost_used = active_block.get("costUSD", 0.0)
        messages = active_block.get("sentMessagesCount", 0)
        duration = active_block.get("durationMinutes", 1) or 1
        tc = active_block.get("tokenCounts", {})
        io_tokens = tc.get("inputTokens", 0) + tc.get("outputTokens", 0)
        burn_rate = io_tokens / duration

        try:
            end_time = datetime.fromisoformat(str(active_block.get("endTime", "")))
            reset_min = max(0, int((end_time - datetime.now(timezone.utc)).total_seconds() / 60))
        except (ValueError, TypeError):
            reset_min = 300

        per_model = active_block.get("perModelStats", {})
        short_model, model_pct = "Sonnet", 0
        if per_model:
            total_entries = sum(s.get("entries_count", 0) for s in per_model.values())
            for m, stats in per_model.items():
                p = int(stats.get("entries_count", 0) * 100 / max(total_entries, 1))
                if p > model_pct:
                    model_pct = p
                    ml = m.lower()
                    short_model = "Opus" if "opus" in ml else "Haiku" if "haiku" in ml else "Sonnet"

        projection = active_block.get("projection", {})
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
        duration = 1

    pct = total_tokens / token_limit if token_limit > 0 else 0
    warning = 3 if pct >= 0.90 else 2 if pct >= 0.75 else 1 if pct >= 0.50 else 0

    plan_cfg = Plans.get_plan_by_name(plan) or Plans.get_plan_by_name("custom")

    return {
        "tokens_used":        total_tokens,
        "tokens_limit":       token_limit,
        "cost_used":          round(cost_used, 2),
        "cost_limit":         round(cost_limit, 2),
        "msgs_used":          messages,
        "msgs_limit":         msg_limit,
        "burn_rate":          round(burn_rate, 1),
        "cost_rate":          round(cost_used / duration, 4),
        "depletion_min":      min(int(depletion_min), 999),
        "reset_min":          min(reset_min, 300),
        "session_elapsed_min": min(300 - reset_min, 300),
        "session_total_min":  300,
        "plan_name":          plan_cfg.display_name,
        "model":              short_model,
        "model_pct":          model_pct,
        "warning_level":      warning,
    }


def post_json(url: str, payload: dict, timeout: int = 5) -> bool:
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    req = urllib.request.Request(url, data=body,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status == 200
    except Exception:
        return False


def check_device(host: str, timeout: int = 3) -> bool:
    try:
        url = f"http://{host}/api/status"
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return resp.status == 200
    except Exception:
        return False


def main():
    parser = argparse.ArgumentParser(description="WiFi bridge: claude-monitor → CYD HTTP")
    parser.add_argument("--plan", choices=["pro", "max5", "max20", "custom"],
                        default="pro", help="Plan type (default: pro)")
    parser.add_argument("--host", default="claude-monitor.local",
                        help="CYD hostname or IP (default: claude-monitor.local)")
    parser.add_argument("--interval", type=float, default=30.0,
                        help="Update interval in seconds (default: 30)")
    args = parser.parse_args()

    monitor_url = f"http://{args.host}/api/monitor"

    print("Claude Monitor CYD — WiFi Bridge")
    print(f"  Host:     {args.host}")
    print(f"  URL:      {monitor_url}")
    print(f"  Plan:     {args.plan}")
    print(f"  Interval: {args.interval}s")
    print()

    print(f"Checking device at {args.host}...", end=" ", flush=True)
    if check_device(args.host):
        print("online")
    else:
        print("not reachable")
        print("  Make sure the CYD is on the same WiFi and firmware v3 is flashed.")
        print("  Run setup_wifi.py first if you haven't already.")
        sys.exit(1)

    print()
    consecutive_failures = 0

    try:
        while True:
            try:
                payload = build_payload(args.plan)
            except Exception as e:
                print(f"\r[ERROR] build_payload: {e}  ", end="", flush=True)
                time.sleep(args.interval)
                continue

            ok = post_json(monitor_url, payload)
            if ok:
                consecutive_failures = 0
                pct = int(payload["tokens_used"] * 100 / max(payload["tokens_limit"], 1))
                status = ["OK", "CAUTION", "WARNING", "CRITICAL"][payload["warning_level"]]
                print(
                    f"\r  {payload['plan_name']}  "
                    f"Tok: {payload['tokens_used']:>6}/{payload['tokens_limit']}({pct}%)  "
                    f"${payload['cost_used']:.2f}/${payload['cost_limit']:.0f}  "
                    f"Msg: {payload['msgs_used']}/{payload['msgs_limit']}  "
                    f"Burn: {payload['burn_rate']:.1f}t/m  "
                    f"{payload['model']} {payload['model_pct']}%  "
                    f"[{status}]  ",
                    end="", flush=True,
                )
            else:
                consecutive_failures += 1
                print(f"\r[WARN] POST failed ({consecutive_failures}x) — device offline?  ",
                      end="", flush=True)
                if consecutive_failures >= 5:
                    print(f"\n[ERROR] CYD unreachable for {consecutive_failures} attempts. "
                          "Check WiFi connection.")

            time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\n\nBridge stopped.")


if __name__ == "__main__":
    main()
