#!/usr/bin/env python3
"""
bridge_claude_monitor.py - Bridge between claude-monitor and CYD hardware

Reads real Claude Code session data from ~/.claude/projects/*.jsonl
(the same data source as claude-monitor v3.1) and sends compact JSON
to the CYD over USB serial.

Usage:
    python bridge_claude_monitor.py --port COM3 --plan max20
    python bridge_claude_monitor.py --port COM3 --plan pro --interval 5

Requirements:
    pip install pyserial
"""

import json
import os
import sys
import time
import glob
import argparse
import hashlib
from pathlib import Path
from datetime import datetime, timedelta, timezone

# ── Configuration ──────────────────────────────────────────

CLAUDE_PROJECTS_DIR = Path("~/.claude/projects").expanduser()
SERIAL_BAUD = 115200

# Plan limits (synced with claude-monitor v3.1)
PLANS = {
    "pro":   {"tokens": 19000,  "cost": 18.0,  "messages": 250},
    "max5":  {"tokens": 88000,  "cost": 35.0,  "messages": 1000},
    "max20": {"tokens": 220000, "cost": 140.0, "messages": 2000},
}

# Model pricing ($/1K tokens) - synced with claude-monitor
MODEL_PRICING = {
    "claude-sonnet-4":         {"input": 0.003,  "output": 0.015},
    "claude-opus-4":           {"input": 0.015,  "output": 0.075},
    "claude-haiku-4":          {"input": 0.001,  "output": 0.005},
    "claude-3-5-sonnet":       {"input": 0.003,  "output": 0.015},
    "claude-3-opus":           {"input": 0.015,  "output": 0.075},
    "claude-3-haiku":          {"input": 0.00025, "output": 0.00125},
    "default":                 {"input": 0.003,  "output": 0.015},
}


def find_jsonl_files():
    """Find all .jsonl files in ~/.claude/projects/ (same as claude-monitor)."""
    if not CLAUDE_PROJECTS_DIR.exists():
        return []
    return list(CLAUDE_PROJECTS_DIR.rglob("*.jsonl"))


def parse_jsonl_entries(hours_back=5):
    """Parse JSONL entries from the last N hours, deduplicated."""
    cutoff = datetime.now(timezone.utc) - timedelta(hours=hours_back)
    files = find_jsonl_files()

    entries = []
    seen_hashes = set()

    for fp in files:
        try:
            with open(fp, "r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        data = json.loads(line)
                    except json.JSONDecodeError:
                        continue

                    # Parse timestamp
                    ts_str = data.get("timestamp", "")
                    if not ts_str:
                        continue
                    try:
                        ts = datetime.fromisoformat(ts_str.replace("Z", "+00:00"))
                        if ts.tzinfo is None:
                            ts = ts.replace(tzinfo=timezone.utc)
                    except (ValueError, TypeError):
                        continue

                    if ts < cutoff:
                        continue

                    # Deduplicate by message.id + uuid
                    msg = data.get("message", {})
                    msg_id = ""
                    if isinstance(msg, dict):
                        msg_id = msg.get("id", "")
                    msg_id = msg_id or data.get("message_id", "")
                    uuid = data.get("uuid", "")
                    if msg_id:
                        h = f"{msg_id}:{uuid}"
                        if h in seen_hashes:
                            continue
                        seen_hashes.add(h)

                    # Extract tokens - claude stores usage inside message.usage
                    message = data.get("message", {})
                    if not isinstance(message, dict):
                        continue

                    usage = message.get("usage", {})
                    if not isinstance(usage, dict):
                        # Also check root-level usage
                        usage = data.get("usage", {})
                        if not isinstance(usage, dict):
                            continue

                    input_tokens = usage.get("input_tokens", 0)
                    output_tokens = usage.get("output_tokens", 0)
                    cache_creation = usage.get("cache_creation_input_tokens", 0)
                    cache_read = usage.get("cache_read_input_tokens", 0)

                    # Total: input + output only (cache tokens are free/discounted
                    # and not counted toward plan limits by claude-monitor)
                    total = input_tokens + output_tokens
                    if total == 0:
                        continue

                    # Extract model from message object or root
                    model = message.get("model", "") or data.get("model", "default")
                    model_lower = model.lower() if model else "default"

                    # Compute cost
                    pricing = MODEL_PRICING.get("default")
                    for key, p in MODEL_PRICING.items():
                        if key != "default" and key in model_lower:
                            pricing = p
                            break

                    cost = (input_tokens / 1000.0) * pricing["input"] + \
                           (output_tokens / 1000.0) * pricing["output"]

                    entries.append({
                        "timestamp": ts,
                        "input_tokens": input_tokens,
                        "output_tokens": output_tokens,
                        "cache_creation": cache_creation,
                        "cache_read": cache_read,
                        "total_tokens": total,
                        "cost": cost,
                        "model": model,
                    })

        except Exception as e:
            print(f"  Warning: Error reading {fp}: {e}", file=sys.stderr)

    entries.sort(key=lambda e: e["timestamp"])
    return entries


def aggregate_to_cyd_payload(entries, plan_name="max20"):
    """Aggregate entries into the CYD dashboard JSON payload."""
    plan = PLANS.get(plan_name, PLANS["max20"])

    total_tokens = sum(e["total_tokens"] for e in entries)
    total_cost = sum(e["cost"] for e in entries)
    message_count = len(entries)

    # Session timing
    if entries:
        earliest = entries[0]["timestamp"]
        latest = entries[-1]["timestamp"]
        elapsed_min = int((datetime.now(timezone.utc) - earliest).total_seconds() / 60)
    else:
        elapsed_min = 0

    # Burn rate (tokens/min over last 15 minutes)
    now = datetime.now(timezone.utc)
    recent_cutoff = now - timedelta(minutes=15)
    recent_entries = [e for e in entries if e["timestamp"] >= recent_cutoff]
    recent_tokens = sum(e["total_tokens"] for e in recent_entries)
    recent_minutes = max(15, 1)
    burn_rate = int(recent_tokens / recent_minutes) if recent_entries else 0

    # Depletion estimate
    remaining_tokens = max(0, plan["tokens"] - total_tokens)
    if burn_rate > 0:
        depletion_min = int(remaining_tokens / burn_rate)
    else:
        depletion_min = 999

    # Warning level
    pct = (total_tokens / plan["tokens"] * 100) if plan["tokens"] > 0 else 0
    if pct >= 90:
        warning = 3
    elif pct >= 75:
        warning = 2
    elif pct >= 50:
        warning = 1
    else:
        warning = 0

    return {
        "tokens_used": total_tokens,
        "tokens_limit": plan["tokens"],
        "cost_used": round(total_cost, 2),
        "cost_limit": plan["cost"],
        "burn_rate": burn_rate,
        "depletion_min": min(depletion_min, 999),
        "session_elapsed_min": min(elapsed_min, 300),
        "session_total_min": 300,
        "plan_name": plan_name.capitalize(),
        "warning_level": warning,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Claude Monitor -> CYD Bridge (reads real Claude data)"
    )
    parser.add_argument("--port", required=True, help="Serial port (e.g., COM3)")
    parser.add_argument("--plan", choices=["pro", "max5", "max20"],
                        default="max20", help="Your Claude plan (default: max20)")
    parser.add_argument("--interval", type=float, default=5.0,
                        help="Update interval in seconds (default: 5)")
    parser.add_argument("--hours", type=int, default=5,
                        help="Hours of history to scan (default: 5)")
    args = parser.parse_args()

    # Open serial port
    import serial
    try:
        ser = serial.Serial(args.port, SERIAL_BAUD, timeout=1)
        print(f"Connected to {args.port} at {SERIAL_BAUD} baud")
    except Exception as e:
        print(f"ERROR: Cannot open {args.port}: {e}")
        sys.exit(1)

    print(f"Data source: {CLAUDE_PROJECTS_DIR}")
    print(f"Plan: {args.plan}")
    print(f"Interval: {args.interval}s")
    print(f"History: last {args.hours} hours")
    print()

    try:
        while True:
            entries = parse_jsonl_entries(hours_back=args.hours)
            payload = aggregate_to_cyd_payload(entries, plan_name=args.plan)

            # Send compact JSON + newline to CYD
            json_str = json.dumps(payload, separators=(",", ":")) + "\n"
            ser.write(json_str.encode("utf-8"))

            # Console output
            pct = int(payload["tokens_used"] * 100 / max(payload["tokens_limit"], 1))
            status = ["OK", "CAUTION", "WARNING", "CRITICAL"][payload["warning_level"]]
            print(
                f"\r  Tokens: {payload['tokens_used']:>7} / {payload['tokens_limit']}  "
                f"({pct}%)  ${payload['cost_used']:.2f}  "
                f"Burn: {payload['burn_rate']}t/m  [{status}]  "
                f"({len(entries)} entries)",
                end="", flush=True
            )

            time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\n\nBridge stopped.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
