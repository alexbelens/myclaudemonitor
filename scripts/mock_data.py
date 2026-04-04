#!/usr/bin/env python3
"""
mock_data.py — Claude Monitor Mock Data Generator

Simulates a realistic Claude Code session by writing JSON to
/tmp/claude_monitor_data.json every 2 seconds.

The LVGL simulator reads this file to update the dashboard.

Usage:
    python3 mock_data.py          # Run continuously (live simulation)
    python3 mock_data.py --once   # Write one snapshot and exit
    python3 mock_data.py --demo   # Fast demo mode (10x speed)

When your CYD hardware arrives, replace this with the real bridge
script (bridge_serial.py) that reads from claude-monitor's data.
"""

import json
import math
import os
import sys
import time
import random
import argparse
from pathlib import Path

import tempfile
DATA_FILE = os.path.join(tempfile.gettempdir(), "claude_monitor_data.json")

# ── Plan definitions (mirrors claude-monitor's config) ──────────────

PLANS = {
    "pro":   {"tokens_limit": 19000,  "cost_limit": 18.0,  "messages_limit": 250},
    "max5":  {"tokens_limit": 88000,  "cost_limit": 35.0,  "messages_limit": 1000},
    "max20": {"tokens_limit": 220000, "cost_limit": 140.0, "messages_limit": 2000},
    "custom": {"tokens_limit": 50000, "cost_limit": 50.0,  "messages_limit": 500},
}


class SessionSimulator:
    """Simulates a realistic Claude Code coding session."""

    def __init__(self, plan: str = "pro", speed: float = 1.0):
        self.plan = PLANS.get(plan, PLANS["pro"])
        self.plan_name = plan.capitalize()
        self.speed = speed

        # Session state
        self.session_total_min = 300  # 5 hours
        self.session_start = time.time()
        self.tokens_used = 0
        self.cost_used = 0.0
        self.messages_sent = 0

        # Simulation params
        self.base_burn_rate = random.randint(20, 60)  # tokens/min
        self.cost_per_token = 0.001  # ~$1 per 1000 tokens (rough avg)
        self.burst_probability = 0.15
        self.idle_probability = 0.10

    @property
    def elapsed_min(self) -> int:
        real_elapsed = (time.time() - self.session_start) / 60.0
        return int(real_elapsed * self.speed)

    @property
    def burn_rate(self) -> int:
        """Current burn rate with realistic variation."""
        base = self.base_burn_rate
        # Add some noise
        noise = random.gauss(0, base * 0.15)
        # Occasional burst (large refactor, long response)
        if random.random() < self.burst_probability:
            burst = base * random.uniform(2.0, 5.0)
            return int(base + burst + noise)
        # Occasional idle (reading docs, thinking)
        if random.random() < self.idle_probability:
            return max(0, int(base * 0.1 + noise))
        return max(0, int(base + noise))

    @property
    def warning_level(self) -> int:
        pct = (self.tokens_used / self.plan["tokens_limit"]) * 100 if self.plan["tokens_limit"] > 0 else 0
        if pct >= 90:
            return 3  # critical
        elif pct >= 75:
            return 2  # warning
        elif pct >= 50:
            return 1  # caution
        return 0      # ok

    def tick(self, interval_sec: float = 2.0):
        """Advance the simulation by one interval."""
        burn = self.burn_rate
        tokens_this_tick = int(burn * (interval_sec / 60.0) * self.speed)

        self.tokens_used = min(self.tokens_used + tokens_this_tick, self.plan["tokens_limit"])
        self.cost_used = min(
            self.cost_used + tokens_this_tick * self.cost_per_token,
            self.plan["cost_limit"]
        )

        if tokens_this_tick > 0:
            self.messages_sent += random.choice([0, 0, 0, 1])  # ~25% chance per tick

        # Calculate depletion estimate
        remaining_tokens = self.plan["tokens_limit"] - self.tokens_used
        if burn > 0:
            depletion_min = int(remaining_tokens / burn)
        else:
            depletion_min = 999

        return {
            "tokens_used": self.tokens_used,
            "tokens_limit": self.plan["tokens_limit"],
            "cost_used": round(self.cost_used, 2),
            "cost_limit": self.plan["cost_limit"],
            "burn_rate": burn,
            "depletion_min": min(depletion_min, 999),
            "session_elapsed_min": min(self.elapsed_min, self.session_total_min),
            "session_total_min": self.session_total_min,
            "plan_name": self.plan_name,
            "warning_level": self.warning_level,
            "messages_sent": self.messages_sent,
            "messages_limit": self.plan["messages_limit"],
        }


def write_data(data: dict):
    """Atomically write JSON data to the shared file."""
    tmp_path = DATA_FILE + ".tmp"
    with open(tmp_path, "w") as f:
        json.dump(data, f, indent=2)
    os.replace(tmp_path, DATA_FILE)  # atomic on POSIX


def write_snapshot(plan: str = "pro"):
    """Write a single mid-session snapshot (for --once mode)."""
    data = {
        "tokens_used": 8432,
        "tokens_limit": PLANS[plan]["tokens_limit"],
        "cost_used": 7.65,
        "cost_limit": PLANS[plan]["cost_limit"],
        "burn_rate": 38,
        "depletion_min": 162,
        "session_elapsed_min": 127,
        "session_total_min": 300,
        "plan_name": plan.capitalize(),
        "warning_level": 1,
        "messages_sent": 42,
        "messages_limit": PLANS[plan]["messages_limit"],
    }
    write_data(data)
    print(f"Wrote snapshot to {DATA_FILE}")
    print(json.dumps(data, indent=2))


def run_continuous(plan: str = "pro", speed: float = 1.0, interval: float = 2.0):
    """Run the simulator continuously, updating the data file."""
    sim = SessionSimulator(plan=plan, speed=speed)

    print(f"Claude Monitor Mock Data Generator")
    print(f"  Plan: {sim.plan_name}")
    print(f"  Speed: {speed}x")
    print(f"  Interval: {interval}s")
    print(f"  Output: {DATA_FILE}")
    print(f"  Press Ctrl+C to stop.\n")

    try:
        while True:
            data = sim.tick(interval)
            write_data(data)

            # Console output
            pct = int(data["tokens_used"] * 100 / max(data["tokens_limit"], 1))
            bar_len = 30
            filled = int(bar_len * pct / 100)
            bar = "█" * filled + "░" * (bar_len - filled)

            status = ["OK", "CAUTION", "WARNING", "CRITICAL"][data["warning_level"]]
            print(
                f"\r  [{bar}] {pct:3d}%  "
                f"${data['cost_used']:6.2f}  "
                f"Burn:{data['burn_rate']:3d}t/m  "
                f"[{status}]  ",
                end="", flush=True
            )

            if data["tokens_used"] >= data["tokens_limit"]:
                print("\n\n  Session limit reached! Resetting in 5s...")
                time.sleep(5)
                sim = SessionSimulator(plan=plan, speed=speed)
                print("  New session started.\n")

            time.sleep(interval)

    except KeyboardInterrupt:
        print("\n\nStopped.")


def main():
    parser = argparse.ArgumentParser(description="Claude Monitor mock data generator")
    parser.add_argument("--plan", choices=["pro", "max5", "max20", "custom"],
                        default="pro", help="Simulated plan (default: pro)")
    parser.add_argument("--once", action="store_true",
                        help="Write a single snapshot and exit")
    parser.add_argument("--demo", action="store_true",
                        help="Fast demo mode (10x speed)")
    parser.add_argument("--speed", type=float, default=1.0,
                        help="Simulation speed multiplier (default: 1.0)")
    parser.add_argument("--interval", type=float, default=2.0,
                        help="Update interval in seconds (default: 2.0)")

    args = parser.parse_args()

    if args.once:
        write_snapshot(args.plan)
    else:
        speed = 10.0 if args.demo else args.speed
        run_continuous(plan=args.plan, speed=speed, interval=args.interval)


if __name__ == "__main__":
    main()
