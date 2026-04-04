#!/usr/bin/env python3
"""
bridge_live.py - Live bridge using claude-monitor's own analysis engine

Uses the EXACT same data pipeline as `claude-monitor --view realtime`:
- analyze_usage() for session blocks
- AdvancedCustomLimitDisplay for P90 cost/message limits
- Input+output token burn rate (excluding cache)

Run with:
    uv tool run --from claude-monitor --with pyserial python scripts/bridge_live.py --port COM3
"""

import json
import sys
import time
import argparse
from datetime import datetime, timezone

from claude_monitor.data.analysis import analyze_usage
from claude_monitor.core.plans import Plans, get_token_limit, get_cost_limit
from claude_monitor.ui.display_controller import AdvancedCustomLimitDisplay


SERIAL_BAUD = 115200


def extract_cyd_payload(data, plan="custom"):
    """Convert claude-monitor analyze_usage() output to CYD JSON payload.

    Uses the exact same P90 limit calculation as the terminal UI.
    """
    blocks = data.get("blocks", [])

    # Token limit via P90
    token_limit = get_token_limit(plan, blocks)

    # Cost + message limits via P90 (same as display_controller.py)
    if plan == "custom":
        try:
            display = AdvancedCustomLimitDisplay(None)
            session_data = display._collect_session_data(blocks)
            percentiles = display._calculate_session_percentiles(
                session_data["limit_sessions"]
            )
            cost_limit = percentiles["costs"]["p90"]
            msg_limit = int(percentiles["messages"]["p90"])
        except Exception:
            cost_limit = get_cost_limit(plan)
            msg_limit = Plans.get_message_limit(plan)
    else:
        cost_limit = get_cost_limit(plan)
        msg_limit = Plans.get_message_limit(plan)

    plan_cfg = Plans.get_plan_by_name(plan) or Plans.get_plan_by_name("custom")
    display_name = plan_cfg.display_name

    # Find the active block
    active_block = None
    for b in blocks:
        if b.get("isActive", False):
            active_block = b
            break

    if active_block:
        total_tokens = active_block.get("totalTokens", 0)
        cost_used = active_block.get("costUSD", 0.0)
        messages = active_block.get("sentMessagesCount", 0)
        duration = active_block.get("durationMinutes", 1) or 1

        # Burn rate: input + output only (same as terminal display)
        tc = active_block.get("tokenCounts", {})
        io_tokens = tc.get("inputTokens", 0) + tc.get("outputTokens", 0)
        burn_rate = io_tokens / duration if duration > 0 else 0.0

        # Cost rate: total cost / duration
        cost_per_min = cost_used / duration if duration > 0 else 0.0

        # Model distribution
        models = active_block.get("models", [])
        per_model = active_block.get("perModelStats", {})
        short_model = "--"
        model_pct = 0
        if per_model:
            total_entries = sum(s.get("entries_count", 0) for s in per_model.values())
            for m, stats in per_model.items():
                p = int(stats.get("entries_count", 0) * 100 / max(total_entries, 1))
                if p > model_pct:
                    model_pct = p
                    ml = m.lower()
                    if "opus" in ml:
                        short_model = "Opus"
                    elif "sonnet" in ml:
                        short_model = "Sonnet"
                    elif "haiku" in ml:
                        short_model = "Haiku"
                    else:
                        short_model = m[:10]

        # Time to reset
        end_time_str = active_block.get("endTime", "")
        try:
            end_time = datetime.fromisoformat(str(end_time_str))
            now = datetime.now(timezone.utc)
            remaining_sec = max(0, (end_time - now).total_seconds())
            reset_min = int(remaining_sec / 60)
        except (ValueError, TypeError):
            reset_min = 300

        session_elapsed_min = max(0, 300 - reset_min)

        # Depletion estimate
        projection = active_block.get("projection", {})
        if projection and "remainingMinutes" in projection:
            depletion_min = int(projection["remainingMinutes"])
        elif burn_rate > 0:
            remaining_tokens = max(0, token_limit - total_tokens)
            depletion_min = int(remaining_tokens / burn_rate)
        else:
            depletion_min = 999
    else:
        total_tokens = data.get("total_tokens", 0)
        cost_used = data.get("total_cost", 0.0)
        burn_rate = 0.0
        cost_per_min = 0.0
        messages = 0
        session_elapsed_min = 0
        reset_min = 300
        depletion_min = 999
        short_model = "--"
        model_pct = 0

    # Warning level
    pct = (total_tokens / token_limit * 100) if token_limit > 0 else 0
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
        "tokens_limit": token_limit,
        "cost_used": round(cost_used, 2),
        "cost_limit": round(cost_limit, 2),
        "msgs_used": messages,
        "msgs_limit": msg_limit,
        "burn_rate": round(burn_rate, 1),
        "cost_rate": round(cost_per_min, 4),
        "depletion_min": min(depletion_min, 999),
        "reset_min": min(reset_min, 300),
        "session_elapsed_min": min(session_elapsed_min, 300),
        "session_total_min": 300,
        "plan_name": display_name,
        "model": short_model,
        "model_pct": model_pct,
        "warning_level": warning,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Live bridge: claude-monitor realtime -> CYD display"
    )
    parser.add_argument("--port", required=True, help="Serial port (e.g., COM3)")
    parser.add_argument("--plan", choices=["pro", "max5", "max20", "custom"],
                        default="custom",
                        help="Plan type (default: custom = P90 auto-detect)")
    parser.add_argument("--interval", type=float, default=10.0,
                        help="Update interval in seconds (default: 10)")
    args = parser.parse_args()

    import serial
    try:
        ser = serial.Serial(args.port, SERIAL_BAUD, timeout=1)
    except Exception as e:
        print(f"ERROR: Cannot open {args.port}: {e}")
        sys.exit(1)

    print(f"Claude Monitor CYD Live Bridge")
    print(f"  Port: {args.port} @ {SERIAL_BAUD} baud")
    print(f"  Plan: {args.plan}" + (" (P90 auto-detect)" if args.plan == "custom" else ""))
    print(f"  Interval: {args.interval}s")
    print(f"  Engine: claude-monitor v3.1 (same as --view realtime)")
    print()

    try:
        while True:
            data = analyze_usage(hours_back=192, quick_start=False, use_cache=False)
            payload = extract_cyd_payload(data, plan=args.plan)

            json_str = json.dumps(payload, separators=(",", ":")) + "\n"
            ser.write(json_str.encode("utf-8"))

            pct = int(payload["tokens_used"] * 100 / max(payload["tokens_limit"], 1))
            status = ["OK", "CAUTION", "WARNING", "CRITICAL"][payload["warning_level"]]
            print(
                f"\r  {payload['plan_name']}  "
                f"Tok: {payload['tokens_used']:>6}/{payload['tokens_limit']}({pct}%)  "
                f"${payload['cost_used']:.2f}/${payload['cost_limit']:.0f}  "
                f"Msg: {payload['msgs_used']}/{payload['msgs_limit']}  "
                f"Burn: {payload['burn_rate']:.1f}t/m  "
                f"${payload['cost_rate']:.4f}/m  "
                f"{payload['model']} {payload['model_pct']}%  "
                f"[{status}]  ",
                end="", flush=True
            )

            time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\n\nBridge stopped.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
