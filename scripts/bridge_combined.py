#!/usr/bin/env python3
"""
bridge_combined.py - Combined monitor + window manager bridge

Sends both claude-monitor data and window list to CYD over a single serial port.
Receives switch commands from CYD to bring windows to front.

Run with:
    uv tool run --from claude-monitor --with pyserial --with pywin32 python scripts/bridge_combined.py --port COM3

Or if pywin32 is installed system-wide:
    uv tool run --from claude-monitor --with pyserial python scripts/bridge_combined.py --port COM3
"""

import json
import sys
import time
import re
import threading
import argparse
from datetime import datetime, timezone

# claude-monitor imports
from claude_monitor.data.analysis import analyze_usage
from claude_monitor.core.plans import Plans, get_token_limit, get_cost_limit
from claude_monitor.ui.display_controller import AdvancedCustomLimitDisplay

# Windows API
import win32gui
import win32con
import win32process

SERIAL_BAUD = 115200

# ── Window enumeration ──────────────────────────────────────

# Window title patterns to match CLI/terminal windows
CLI_PATTERNS = [
    "powershell",
    "cmd.exe",
    "terminal",
    "windows terminal",
    "claude code",
    "claude",
    "cursor",
]

# Patterns to exclude (not CLI windows)
EXCLUDE_PATTERNS = [
    "google chrome",
    "microsoft teams",
    "outlook",
    "excel",
    "docker",
    "settings",
    "file explorer",
    "notepad",
    "gmail",
]


def shorten_title(title):
    """Extract a meaningful short name from a window title."""
    t = title.strip()

    # "Windows PowerShell" → try to get the directory from the title
    if t == "Windows PowerShell":
        return "PowerShell"

    # "PS C:\Users\...\project>" → extract project name
    m = re.search(r'[\\\/]([^\\\/]+?)(?:[\\\/]?>?\s*$)', t)
    if m:
        return m.group(1)[:20]

    # Claude Code sessions: "✳ Claude Code" or "✳ Something"
    if t.startswith("\u2733"):
        name = t.lstrip("\u2733 ").strip()
        return name[:20] if name else "Claude"

    # "name - Cursor" → extract project name
    if " - Cursor" in t:
        return t.split(" - Cursor")[0].strip()[:20]

    # Generic: take first 20 chars
    return t[:20]


def enumerate_cli_windows():
    """Find all visible CLI/terminal/IDE windows."""
    results = []

    def callback(hwnd, _):
        if not win32gui.IsWindowVisible(hwnd):
            return
        title = win32gui.GetWindowText(hwnd)
        if not title:
            return

        title_lower = title.lower()

        # Skip excluded patterns
        for excl in EXCLUDE_PATTERNS:
            if excl in title_lower:
                return

        # Match CLI patterns
        is_cli = False
        for pat in CLI_PATTERNS:
            if pat in title_lower:
                is_cli = True
                break

        # Also match "✳" prefix (Claude Code windows)
        if title.startswith("\u2733"):
            is_cli = True

        if is_cli:
            short_name = shorten_title(title)
            results.append({
                "id": hwnd,
                "name": short_name,
            })

    win32gui.EnumWindows(callback, None)
    return results[:10]  # Max 10 windows


def switch_to_window(hwnd):
    """Bring a window to the foreground."""
    try:
        # Restore if minimized
        if win32gui.IsIconic(hwnd):
            win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
        # Bring to front
        win32gui.SetForegroundWindow(hwnd)
        return True
    except Exception as e:
        print(f"\n  Warning: Could not switch to window {hwnd}: {e}")
        return False


# ── Monitor data extraction (same as bridge_live.py) ────────

def extract_monitor_payload(data, plan="custom"):
    """Extract monitor data using claude-monitor's analysis."""
    blocks = data.get("blocks", [])
    token_limit = get_token_limit(plan, blocks)

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

        tc = active_block.get("tokenCounts", {})
        io_tokens = tc.get("inputTokens", 0) + tc.get("outputTokens", 0)
        burn_rate = io_tokens / duration if duration > 0 else 0.0
        cost_per_min = cost_used / duration if duration > 0 else 0.0

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
                    if "opus" in ml: short_model = "Opus"
                    elif "sonnet" in ml: short_model = "Sonnet"
                    elif "haiku" in ml: short_model = "Haiku"
                    else: short_model = m[:10]

        end_time_str = active_block.get("endTime", "")
        try:
            end_time = datetime.fromisoformat(str(end_time_str))
            remaining_sec = max(0, (end_time - datetime.now(timezone.utc)).total_seconds())
            reset_min = int(remaining_sec / 60)
        except (ValueError, TypeError):
            reset_min = 300

        session_elapsed_min = max(0, 300 - reset_min)

        projection = active_block.get("projection", {})
        if projection and "remainingMinutes" in projection:
            depletion_min = int(projection["remainingMinutes"])
        elif burn_rate > 0:
            depletion_min = int(max(0, token_limit - total_tokens) / burn_rate)
        else:
            depletion_min = 999
    else:
        total_tokens = data.get("total_tokens", 0)
        cost_used = data.get("total_cost", 0.0)
        burn_rate = cost_per_min = 0.0
        messages = session_elapsed_min = 0
        reset_min = 300
        depletion_min = 999
        short_model = "--"
        model_pct = 0

    pct = (total_tokens / token_limit * 100) if token_limit > 0 else 0
    warning = 3 if pct >= 90 else 2 if pct >= 75 else 1 if pct >= 50 else 0

    return {
        "type": "monitor",
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
        "plan_name": plan_cfg.display_name,
        "model": short_model,
        "model_pct": model_pct,
        "warning_level": warning,
    }


# ── Serial listener (reads switch commands from CYD) ────────

class BridgeState:
    """Mutable state shared between main loop and serial listener."""
    def __init__(self, plan="custom"):
        self.plan = plan
        self.view = "realtime"
        self.config_changed = False

_state = BridgeState()

def serial_listener(ser):
    """Background thread: reads CYD → PC commands (switch + config)."""
    global _state
    buf = ""
    while True:
        try:
            if ser.in_waiting:
                chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
                buf += chunk
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        cmd = json.loads(line)
                        if "switch" in cmd:
                            hwnd = int(cmd["switch"])
                            print(f"\n  >> Switch to window {hwnd}")
                            switch_to_window(hwnd)
                        if "config" in cmd:
                            cfg = cmd["config"]
                            if "plan" in cfg:
                                new_plan = cfg["plan"]
                                if new_plan in ("custom", "pro", "max5", "max20"):
                                    _state.plan = new_plan
                                    _state.config_changed = True
                                    print(f"\n  >> Plan changed to: {new_plan}")
                            if "view" in cfg:
                                new_view = cfg["view"]
                                if new_view in ("realtime", "daily", "monthly"):
                                    _state.view = new_view
                                    _state.config_changed = True
                                    print(f"\n  >> View changed to: {new_view}")
                    except (json.JSONDecodeError, ValueError):
                        pass
            else:
                time.sleep(0.1)
        except Exception:
            time.sleep(0.5)


# ── Main loop ───────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Combined bridge: claude-monitor + window switcher -> CYD"
    )
    parser.add_argument("--port", required=True, help="Serial port (e.g., COM3)")
    parser.add_argument("--plan", choices=["pro", "max5", "max20", "custom"],
                        default="custom")
    parser.add_argument("--monitor-interval", type=float, default=10.0,
                        help="Monitor update interval (default: 10s)")
    parser.add_argument("--window-interval", type=float, default=3.0,
                        help="Window list update interval (default: 3s)")
    args = parser.parse_args()

    import serial
    try:
        ser = serial.Serial(args.port, SERIAL_BAUD, timeout=0.1)
    except Exception as e:
        print(f"ERROR: Cannot open {args.port}: {e}")
        sys.exit(1)

    # Initialize shared state with CLI args
    global _state
    _state = BridgeState(plan=args.plan)

    print(f"Claude Monitor CYD Combined Bridge")
    print(f"  Port: {args.port} @ {SERIAL_BAUD}")
    print(f"  Plan: {args.plan} (changeable from CYD Settings screen)")
    print(f"  Monitor interval: {args.monitor_interval}s")
    print(f"  Window interval: {args.window_interval}s")
    print()

    # Start background listener for switch + config commands from CYD
    listener = threading.Thread(target=serial_listener, args=(ser,), daemon=True)
    listener.start()

    last_monitor = 0
    last_windows = 0

    try:
        while True:
            now = time.time()

            # If config changed from CYD, force immediate monitor refresh
            if _state.config_changed:
                _state.config_changed = False
                last_monitor = 0  # Force refresh on next tick

            # Send monitor data
            if now - last_monitor >= args.monitor_interval:
                try:
                    current_plan = _state.plan
                    data = analyze_usage(hours_back=192, quick_start=False, use_cache=False)
                    payload = extract_monitor_payload(data, plan=current_plan)
                    ser.write((json.dumps(payload, separators=(",", ":")) + "\n").encode())
                    last_monitor = now

                    pct = int(payload["tokens_used"] * 100 / max(payload["tokens_limit"], 1))
                    status = ["OK", "CAUTION", "WARNING", "CRITICAL"][payload["warning_level"]]
                    print(
                        f"\r  [{current_plan}] "
                        f"Tok: {payload['tokens_used']:>6}/{payload['tokens_limit']}({pct}%) "
                        f"${payload['cost_used']:.0f}/${payload['cost_limit']:.0f} "
                        f"Burn:{payload['burn_rate']:.0f}t/m "
                        f"[{status}]  ",
                        end="", flush=True
                    )
                except Exception as e:
                    print(f"\n  Monitor error: {e}")

            # Send window list
            if now - last_windows >= args.window_interval:
                try:
                    windows = enumerate_cli_windows()
                    win_payload = {
                        "type": "windows",
                        "list": windows,
                    }
                    ser.write((json.dumps(win_payload, separators=(",", ":")) + "\n").encode())
                    last_windows = now
                except Exception as e:
                    print(f"\n  Window enum error: {e}")

            time.sleep(0.5)

    except KeyboardInterrupt:
        print("\n\nBridge stopped.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
