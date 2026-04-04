#!/usr/bin/env python3
"""
bridge_serial.py — Claude Monitor → CYD Bridge

Reads real Claude Code session data from ~/.config/claude/
(the same source claude-monitor uses) and outputs JSON to
either a file (for simulator) or a serial port (for hardware).

Usage:
    # For LVGL PC simulator (writes to /tmp file):
    python3 bridge_serial.py --output file

    # For real ESP32 CYD hardware over USB:
    python3 bridge_serial.py --output serial --port /dev/ttyUSB0

    # Auto-detect mode (tries serial, falls back to file):
    python3 bridge_serial.py

Requirements:
    pip install pyserial   # only needed for serial output

NOTE: This script reads Claude's raw session data directly.
      For a more robust integration, you can instead import
      claude-monitor's Python modules if installed:
          pip install claude-monitor
"""

import json
import os
import sys
import time
import glob
import argparse
from pathlib import Path
from datetime import datetime, timezone

# ── Configuration ───────────────────────────────────────────

CLAUDE_CONFIG_DIR = os.environ.get(
    "CLAUDE_CONFIG_DIR",
    os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), ".config", "claude")
    if sys.platform == "win32"
    else os.path.expanduser("~/.config/claude")
)
PROJECTS_DIR = os.path.join(CLAUDE_CONFIG_DIR, "projects")
import tempfile
DATA_FILE = os.path.join(tempfile.gettempdir(), "claude_monitor_data.json")
SERIAL_BAUD = 115200

# Plan limits (keep in sync with claude-monitor)
PLANS = {
    "pro":    {"tokens": 19000,  "cost": 18.0},
    "max5":   {"tokens": 88000,  "cost": 35.0},
    "max20":  {"tokens": 220000, "cost": 140.0},
    "custom": {"tokens": 50000,  "cost": 50.0},
}

# Model pricing ($ per 1K tokens, approximate)
MODEL_PRICING = {
    "claude-sonnet-4-20250514":    {"input": 0.003, "output": 0.015},
    "claude-opus-4-20250514":      {"input": 0.015, "output": 0.075},
    "claude-haiku-4-5-20251001":   {"input": 0.001, "output": 0.005},
    # fallback
    "default":                      {"input": 0.003, "output": 0.015},
}


def find_session_files():
    """Find Claude session JSON files from the last 5 hours."""
    paths = []
    five_hours_ago = time.time() - (5 * 3600)

    # Claude stores sessions in various locations depending on version
    search_patterns = [
        os.path.join(CLAUDE_CONFIG_DIR, "*.json"),
        os.path.join(CLAUDE_CONFIG_DIR, "sessions", "*.json"),
        os.path.join(PROJECTS_DIR, "**", "*.json"),
    ]

    for pattern in search_patterns:
        for path in glob.glob(pattern, recursive=True):
            try:
                if os.path.getmtime(path) >= five_hours_ago:
                    paths.append(path)
            except OSError:
                continue

    return paths


def parse_session_data(file_path: str) -> dict:
    """Parse a Claude session file and extract token usage."""
    try:
        with open(file_path, "r") as f:
            data = json.load(f)
    except (json.JSONDecodeError, IOError):
        return {}

    tokens_input = 0
    tokens_output = 0
    tokens_cache = 0
    model = "default"

    # Handle different session file formats
    if isinstance(data, dict):
        usage = data.get("usage", {})
        tokens_input = usage.get("input_tokens", 0)
        tokens_output = usage.get("output_tokens", 0)
        tokens_cache = usage.get("cache_creation_input_tokens", 0) + \
                       usage.get("cache_read_input_tokens", 0)
        model = data.get("model", "default")

    return {
        "tokens_input": tokens_input,
        "tokens_output": tokens_output,
        "tokens_cache": tokens_cache,
        "model": model,
        "mtime": os.path.getmtime(file_path),
    }


def compute_cost(tokens_input: int, tokens_output: int, model: str = "default") -> float:
    """Estimate cost based on model pricing."""
    pricing = MODEL_PRICING.get(model, MODEL_PRICING["default"])
    cost = (tokens_input / 1000.0) * pricing["input"] + \
           (tokens_output / 1000.0) * pricing["output"]
    return round(cost, 4)


def aggregate_sessions(plan: str = "pro") -> dict:
    """Read all active sessions and aggregate into a single data payload."""
    files = find_session_files()

    total_input = 0
    total_output = 0
    total_cost = 0.0
    earliest_mtime = time.time()
    latest_mtime = 0
    session_count = 0

    for fp in files:
        s = parse_session_data(fp)
        if not s:
            continue
        total_input += s["tokens_input"]
        total_output += s["tokens_output"]
        total_cost += compute_cost(s["tokens_input"], s["tokens_output"], s["model"])
        earliest_mtime = min(earliest_mtime, s["mtime"])
        latest_mtime = max(latest_mtime, s["mtime"])
        session_count += 1

    total_tokens = total_input + total_output
    plan_cfg = PLANS.get(plan, PLANS["pro"])

    # Calculate elapsed time
    if session_count > 0:
        elapsed_min = int((time.time() - earliest_mtime) / 60)
    else:
        elapsed_min = 0

    # Estimate burn rate (tokens per minute over last hour)
    if elapsed_min > 0:
        burn_rate = int(total_tokens / max(elapsed_min, 1))
    else:
        burn_rate = 0

    # Depletion estimate
    remaining_tokens = plan_cfg["tokens"] - total_tokens
    depletion_min = int(remaining_tokens / burn_rate) if burn_rate > 0 else 999

    # Warning level
    pct = (total_tokens / plan_cfg["tokens"] * 100) if plan_cfg["tokens"] > 0 else 0
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
        "tokens_limit": plan_cfg["tokens"],
        "cost_used": round(total_cost, 2),
        "cost_limit": plan_cfg["cost"],
        "burn_rate": burn_rate,
        "depletion_min": min(depletion_min, 999),
        "session_elapsed_min": min(elapsed_min, 300),
        "session_total_min": 300,
        "plan_name": plan.capitalize(),
        "warning_level": warning,
    }


def output_to_file(data: dict):
    """Write data to the shared JSON file (for simulator)."""
    tmp = DATA_FILE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=2)
    os.replace(tmp, DATA_FILE)


def output_to_serial(data: dict, ser):
    """Send compact JSON over serial to ESP32."""
    payload = json.dumps(data, separators=(",", ":")) + "\n"
    ser.write(payload.encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description="Claude Monitor → CYD Bridge")
    parser.add_argument("--plan", choices=["pro", "max5", "max20", "custom"],
                        default="pro", help="Your Claude plan")
    parser.add_argument("--output", choices=["file", "serial", "auto"],
                        default="auto", help="Output mode")
    parser.add_argument("--port", default="/dev/ttyUSB0",
                        help="Serial port for ESP32 (default: /dev/ttyUSB0)")
    parser.add_argument("--interval", type=float, default=5.0,
                        help="Poll interval in seconds (default: 5)")

    args = parser.parse_args()

    # Determine output mode
    ser = None
    if args.output == "serial" or args.output == "auto":
        try:
            import serial
            ser = serial.Serial(args.port, SERIAL_BAUD, timeout=1)
            print(f"Connected to {args.port} at {SERIAL_BAUD} baud")
        except Exception as e:
            if args.output == "serial":
                print(f"ERROR: Cannot open {args.port}: {e}")
                sys.exit(1)
            else:
                print(f"Serial not available ({e}), falling back to file output")
                ser = None

    output_mode = "serial" if ser else "file"
    print(f"Output mode: {output_mode}")
    print(f"Claude config: {CLAUDE_CONFIG_DIR}")
    print(f"Plan: {args.plan}")
    print(f"Interval: {args.interval}s")
    print()

    try:
        while True:
            data = aggregate_sessions(args.plan)

            if output_mode == "serial" and ser:
                output_to_serial(data, ser)
            else:
                output_to_file(data)

            pct = int(data["tokens_used"] * 100 / max(data["tokens_limit"], 1))
            print(f"\r  Tokens: {data['tokens_used']:>7} / {data['tokens_limit']}  "
                  f"({pct}%)  ${data['cost_used']:.2f}  "
                  f"Burn: {data['burn_rate']}t/m", end="", flush=True)

            time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\n\nBridge stopped.")
    finally:
        if ser:
            ser.close()


if __name__ == "__main__":
    main()
