#!/usr/bin/env python3
"""
bridge_auto.py — usage bridge for Claude Monitor CYD.

Fetches real utilization (5h + 7d windows) and sends it to the CYD display
via WiFi (claude-monitor.local) or USB serial fallback.

Two data sources:

  1. OAuth (default) — reads the Claude Code token from the macOS Keychain
     (service "Claude Code-credentials"), or ~/.claude/.credentials.json on
     Linux, and reads utilization from the rate-limit headers returned by
     api.anthropic.com. Claude Code refreshes this token itself, so there is
     nothing to paste by hand and nothing that expires on you.

  2. Session key (fallback) — the old path: a claude.ai sessionKey cookie
     passed via --session-key. Kept for comparison; the cookie expires every
     few weeks and has to be re-copied from the browser each time.

Usage:
    python3 scripts/bridge_auto.py                          # OAuth (default)
    python3 scripts/bridge_auto.py --session-key sk-ant-...  # old path
"""

import argparse
import getpass
import json
import os
import re
import socket
import subprocess
import sys
import time
import urllib.request
import zlib
import glob
from datetime import datetime, timezone
from pathlib import Path

# Only the session-key path needs curl_cffi (Cloudflare bypass). Keep the
# import soft so the OAuth path works without it installed.
try:
    from curl_cffi import requests as cf_requests
except ImportError:
    cf_requests = None

CLAUDE_AI    = "https://claude.ai/api"
WIFI_TIMEOUT = 8
SERIAL_BAUD  = 115200

# ── OAuth source (Claude Code credentials) ────────────────────────────────────

KEYCHAIN_SERVICE = "Claude Code-credentials"
CREDENTIALS_PATH = Path.home() / ".claude" / ".credentials.json"

# The endpoint behind Claude Code's own /usage. Plain GET — no model call,
# no tokens burned. Returns a limits[] array: session, weekly_all, and
# per-model weekly_scoped entries (e.g. Fable), each with percent, severity,
# reset time and whether it is the currently binding limit.
USAGE_URL     = "https://api.anthropic.com/api/oauth/usage"
OAUTH_HEADERS = {
    "Accept":          "application/json",
    "anthropic-beta":  "oauth-2025-04-20",
    "User-Agent":      "claude-code/2.1.5",
}

SEVERITY_LEVEL = {"normal": 0, "warning": 2, "critical": 3}


class SessionExpiredError(Exception):
    pass


class TokenUnavailableError(Exception):
    """Keychain unreadable, or the token in it is stale and not yet refreshed."""


def _extract_oauth(blob: str) -> dict:
    """Pull the OAuth object out of a Claude Code credentials blob.

    Stored as JSON, usually nested: {"claudeAiOauth": {"accessToken": ...}}.
    """
    blob = blob.strip()
    if not blob:
        return {}
    try:
        data = json.loads(blob)
    except json.JSONDecodeError:
        m = re.search(r'"accessToken"\s*:\s*"([^"]+)"', blob)
        return {"accessToken": m.group(1)} if m else {}
    if not isinstance(data, dict):
        return {}
    if isinstance(data.get("accessToken"), str):
        return data
    for v in data.values():
        if isinstance(v, dict) and isinstance(v.get("accessToken"), str):
            return v
    return {}


def read_credentials() -> dict:
    """Read Claude Code credentials — Keychain on macOS, file on Linux."""
    if sys.platform == "darwin":
        try:
            out = subprocess.run(
                ["security", "find-generic-password",
                 "-s", KEYCHAIN_SERVICE, "-a", getpass.getuser(), "-w"],
                check=True, capture_output=True, text=True, timeout=10,
            ).stdout
        except subprocess.CalledProcessError as ex:
            raise TokenUnavailableError(
                f"Keychain read failed (rc={ex.returncode}): {ex.stderr.strip()}") from ex
        except (FileNotFoundError, subprocess.TimeoutExpired) as ex:
            raise TokenUnavailableError(f"Keychain access error: {ex}") from ex
    else:
        try:
            out = CREDENTIALS_PATH.read_text()
        except OSError as ex:
            raise TokenUnavailableError(f"Cannot read {CREDENTIALS_PATH}: {ex}") from ex

    cred = _extract_oauth(out)
    if not cred.get("accessToken"):
        raise TokenUnavailableError("No accessToken in Claude Code credentials")
    return cred


def plan_from_credentials(cred: dict) -> str:
    """Human-readable plan name from the credential fields."""
    tier = (cred.get("rateLimitTier") or "").lower()
    if "max_20" in tier:
        return "Max 20x"
    if "max_5" in tier:
        return "Max 5x"
    sub = (cred.get("subscriptionType") or "").lower()
    if sub == "max":
        return "Max"
    if sub == "pro":
        return "Pro"
    return ""


def _usage_get() -> dict:
    cred = read_credentials()
    headers = dict(OAUTH_HEADERS)
    headers["Authorization"] = f"Bearer {cred['accessToken']}"
    req = urllib.request.Request(USAGE_URL, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as ex:
        if ex.code in (401, 403):
            # Access token lasts ~3h and is refreshed by Claude Code itself.
            # If Claude Code has not run in a while the token can be stale —
            # not fatal, we just retry until it is refreshed.
            raise TokenUnavailableError(
                f"HTTP {ex.code} — token stale; Claude Code will refresh it") from ex
        raise


def _pick(limits: list, kind: str) -> dict:
    for l in limits:
        if l.get("kind") == kind:
            return l
    return {}


def oauth_usage() -> dict:
    """Read every limit Claude Code knows about, in one plain GET."""
    data   = _usage_get()
    limits = data.get("limits") or []

    session = _pick(limits, "session")
    weekly  = _pick(limits, "weekly_all")
    # Per-model weekly cap (Fable, Opus, ...) — present only when the plan
    # actually has one. The API may list several; show the hottest.
    scoped_all = [l for l in limits if l.get("kind") == "weekly_scoped"]
    scoped     = max(scoped_all, key=lambda l: l.get("percent") or 0, default={})

    def pct(l: dict) -> int:
        return max(0, min(100, int(round(l.get("percent") or 0))))

    def reset_min(l: dict) -> int:
        return parse_reset_min(l.get("resets_at") or "")

    def sev(l: dict) -> int:
        return SEVERITY_LEVEL.get(l.get("severity") or "normal", 0)

    # Which limit is the binding one right now — that is the one to watch.
    if scoped.get("is_active"):
        active = 3
    elif weekly.get("is_active"):
        active = 7
    else:
        active = 5

    scope_model = ((scoped.get("scope") or {}).get("model") or {})
    scoped_name = scope_model.get("display_name") or ""

    return {
        "fh_pct":        pct(session),
        "fh_reset_min":  reset_min(session),
        "sd_pct":        pct(weekly),
        "sd_reset_min":  reset_min(weekly),
        "sc_pct":        pct(scoped) if scoped else 0,
        "sc_reset_min":  reset_min(scoped) if scoped else 0,
        "sc_name":       scoped_name[:15],
        "active_win":    active,
        "severity":      max(sev(session), sev(weekly), sev(scoped)),
        "fh_ok":         0 if (session.get("locked_reason") or weekly.get("locked_reason")) else 1,
        "sd_ok":         1,
        "fallback_pct":  0,
    }


# ── Claude.ai API (session-key fallback) ──────────────────────────────────────


def api_get(path: str, session_key: str) -> object:
    if cf_requests is None:
        raise RuntimeError("curl_cffi not installed — needed for --session-key mode")
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


def _detect_plan(org: dict) -> str:
    tier = org.get("rate_limit_tier", "")
    if "max_20" in tier:
        return "Max 20x"
    if "max_5" in tier:
        return "Max 5x"
    caps = org.get("capabilities", [])
    if "claude_max" in caps or "max" in tier:
        return "Max"
    if "claude_pro" in caps or "pro" in tier:
        return "Pro"
    return ""


def get_org_info(session_key: str) -> tuple[str, str]:
    orgs = api_get("/organizations", session_key)
    if not orgs:
        raise RuntimeError("No organizations returned by API")
    org = orgs[0]
    return org["uuid"], _detect_plan(org)


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


def session_usage(session_key: str, org_uuid: str) -> dict:
    usage = api_get(f"/organizations/{org_uuid}/usage", session_key)

    fh = usage.get("five_hour", {}) or {}
    sd = usage.get("seven_day", {}) or {}

    # The claude.ai endpoint reports neither the binding window nor the
    # fallback threshold, so those stay neutral in session-key mode.
    return {
        "fh_pct":       min(100, int(fh.get("utilization", 0.0))),
        "fh_reset_min": parse_reset_min(fh.get("resets_at", "")),
        "sd_pct":       min(100, int(sd.get("utilization", 0.0))),
        "sd_reset_min": parse_reset_min(sd.get("resets_at", "")),
        "sc_pct":       0,
        "sc_reset_min": 0,
        "sc_name":      "",
        "active_win":   0,
        "severity":     0,
        "fh_ok":        1,
        "sd_ok":        1,
        "fallback_pct": 0,
    }


# ── Payload ───────────────────────────────────────────────────────────────────


def build_payload(usage: dict, plan_name: str) -> dict:
    # The display should react to whichever limit is hottest, not just the
    # 5-hour one — a per-model weekly cap can bite long before the session does.
    worst   = max(usage["fh_pct"], usage["sd_pct"], usage["sc_pct"])
    warning = 3 if worst >= 90 else 2 if worst >= 75 else 1 if worst >= 50 else 0
    # Never show calmer than the API itself reports.
    warning = max(warning, usage.get("severity", 0))

    return {
        "fh_pct":        usage["fh_pct"],
        "fh_reset_min":  min(usage["fh_reset_min"], 300),
        "sd_pct":        usage["sd_pct"],
        "sd_reset_min":  min(usage["sd_reset_min"], 10080),
        "sc_pct":        usage["sc_pct"],
        "sc_reset_min":  min(usage["sc_reset_min"], 10080),
        "sc_name":       usage["sc_name"],
        "active_win":    usage["active_win"],
        "warning_level": warning,
        "plan_name":     plan_name or "Claude",
    }


# ── Transport helpers ─────────────────────────────────────────────────────────

# Last transport failure, surfaced in the [WAIT] line so a silent
# "No CYD found" never hides the real reason (DNS, refused, timeout).
_last_wifi_error = ""
_host_addr       = {}   # mDNS name -> resolved IP


def host_addr(host: str) -> str:
    # getaddrinfo, not gethostbyname: only the former resolves mDNS ".local"
    # names through mDNSResponder on macOS.
    if host not in _host_addr:
        try:
            info = socket.getaddrinfo(host, 80, socket.AF_INET, socket.SOCK_STREAM)
            _host_addr[host] = info[0][4][0]
        except OSError:
            return host          # let urllib try the name itself
    return _host_addr[host]


def forget_host(host: str) -> None:
    """Drop the cached address — the device may have been given a new one."""
    _host_addr.pop(host, None)


def wifi_available(host: str) -> bool:
    global _last_wifi_error
    try:
        url = f"http://{host_addr(host)}/api/status"
        with urllib.request.urlopen(url, timeout=WIFI_TIMEOUT) as r:
            _last_wifi_error = ""
            return r.status == 200
    except Exception as ex:
        _last_wifi_error = f"{type(ex).__name__}: {ex}"
        # This probe is the only path back into "wifi" mode, so it has to drop
        # the cached address itself. Otherwise a device that came back on a new
        # IP is probed at the old one forever and the bridge never recovers.
        forget_host(host)
        return False


def send_wifi(payload: dict, host: str) -> bool:
    try:
        body = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        req  = urllib.request.Request(
            f"http://{host_addr(host)}/api/monitor", data=body,
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=WIFI_TIMEOUT) as r:
            return r.status == 200
    except Exception:
        return False


# ── Claude Code sessions waiting for a reply ─────────────────────────────────
# scripts/cyd_session_hook.sh keeps one file per session in this directory,
# holding "waiting" or "busy". A session killed without SessionEnd leaves its
# file behind, so anything untouched for 12h is ignored and swept away.
SESSIONS_DIR      = Path.home() / ".claude" / "cyd-sessions"
SESSION_STALE_SEC = 12 * 3600
WORKING_SEC       = 120     # a "busy" session touched this recently is working
WAIT_SHOW_SEC     = 0       # a chat counts as waiting right away: seeing it at
                            # once matters more than the chat you're typing into
                            # showing up after every answer

# The hook appends "<session> <event>" lines to .xp. The device prices them
# (answer 10, reply 5, subagent 15, compaction 20); the bridge only counts,
# and drops an event that repeats the previous one of the same session.
XP_LOG       = SESSIONS_DIR / ".xp"
XP_EVENTS    = {"S": "xs",   # Claude finished an answer
                "Q": "xq",   # you answered a permission or a question
                "A": "xa",   # a subagent started
                "C": "xc"}   # the context was compacted
XP_PUSH_SEC  = 10           # batch event pushes between usage polls
AGENT_STALE_SEC = 30 * 60   # a lost SubagentStop shouldn't count an agent forever


def session_scan() -> tuple[list, int]:
    """Waiting sessions as (sid, kind, age_sec, since), oldest wait first, and
    the number of sessions where Claude acted in the last WORKING_SEC.
    `since` is when this wait began (the state file's mtime)."""
    try:
        entries = list(SESSIONS_DIR.iterdir())
    except OSError:
        return [], 0
    now, waiting, working = time.time(), [], 0
    for f in entries:
        if f.name.startswith(".") or f.name.endswith(".tmp"):
            continue
        try:
            mtime = f.stat().st_mtime
            age = now - mtime
            if age > SESSION_STALE_SEC:
                f.unlink(missing_ok=True)
                continue
            words = f.read_text().split()
        except OSError:
            continue
        if words and words[0] == "waiting" and age >= WAIT_SHOW_SEC:
            waiting.append((f.name, words[1] if len(words) > 1 else "reply", age, int(mtime)))
        elif words and words[0] == "busy" and age <= WORKING_SEC:
            working += 1
    waiting.sort(key=lambda w: -w[2])
    return waiting, working


# ── Chat titles for the waiting list ─────────────────────────────────────────
# The name Claude Code shows in its session list: a /rename wins
# ("custom-title"), else the generated one ("ai-title"), else the project
# folder. Cached per transcript size, so a waiting chat is read once.
_title_cache: dict = {}
WAIT_LIST_MAX = 5
_TITLE_BAD = str.maketrans({c: " " for c in '"\\|~\n\r\t'})


def _last_value(blob: str, key: str, first: bool = False) -> str:
    m = list(re.finditer(r'"%s":"((?:[^"\\]|\\.)*)"' % key, blob))
    if not m:
        return ""
    try:
        return json.loads('"%s"' % m[0 if first else -1].group(1))
    except ValueError:
        return ""


def _fit(text: str, limit: int) -> str:
    """Clean for the wire and cut to the device buffer without splitting a letter."""
    text = " ".join(text.translate(_TITLE_BAD).split())
    while len(text.encode("utf-8")) > limit:
        text = text[:-1]
    return text


def _asks(blob: str) -> bool:
    """Does Claude's last answer end on a question? A Stop only means "your
    turn"; it's a question only if the final paragraph has a '?'."""
    for line in reversed(blob.splitlines()):
        if '"type":"assistant"' not in line:
            continue
        try:
            content = json.loads(line).get("message", {}).get("content") or []
        except ValueError:
            continue
        texts = [b.get("text", "") for b in content if isinstance(b, dict) and b.get("type") == "text"]
        if not texts:
            continue                      # a tool call; the answer text is further up
        last = texts[-1].strip().split("\n\n")[-1]
        return "?" in last
    return False


def chat_info(sid: str) -> tuple[str, str, bool]:
    """(title, project folder, last answer asks a question) of a session."""
    hits = glob.glob(str(Path.home() / ".claude" / "projects" / "*" / f"{sid}.jsonl"))
    if not hits:
        return sid[:8], "", False
    path = hits[0]
    try:
        size = os.path.getsize(path)
    except OSError:
        return sid[:8], "", False
    cached = _title_cache.get(sid)
    if cached and cached[0] == size:
        return cached[1:]
    try:
        with open(path, encoding="utf-8", errors="ignore") as fh:
            blob = fh.read()
    except OSError:
        return sid[:8], "", False
    # Where the chat was started: Claude may cd into subfolders later on.
    cwd = _last_value(blob, "cwd", first=True)
    # A chat started from ~ has no project; the home folder's name isn't one.
    project = "" if os.path.normpath(cwd) == str(Path.home()) else os.path.basename(cwd)
    title = (_last_value(blob, "customTitle") or _last_value(blob, "aiTitle")
             or project or sid[:8])
    title, project = _fit(title, 70), _fit(project, 30)   # device: 72 + 32 bytes
    info = (title, project, _asks(blob))
    _title_cache[sid] = (size,) + info
    return info


def chat_title(sid: str) -> str:
    return chat_info(sid)[0]


def wait_entries(waiting: list) -> list:
    """(kind, minutes, title, project) for the device. Kinds: r finished,
    your turn / a finished on a question / p wants a permission / q a tool
    asks for input."""
    out = []
    for sid, k, age, since in waiting[:WAIT_LIST_MAX]:
        title, project, asks = chat_info(sid)
        kind = {"permission": "p", "question": "q"}.get(k, "a" if asks else "r")
        # One id per wait, not per chat: hiding it on the device lasts until
        # this chat starts waiting again.
        wid = f"{zlib.crc32(f'{sid}:{since}'.encode()) & 0xffffffff:08x}"
        out.append((wid, kind, int(age // 60), title, project))
    return out


def wait_list(entries: list) -> str:
    """'1a2b3c4d|r|12|CYD monitor|claude-monitor-cyd~...' — wait id, kind,
    minutes, title, project."""
    return "~".join("|".join(str(x) for x in e) for e in entries)


_last_event: dict = {}        # session -> its last XP event, for de-duplication
_agents: dict = {}            # session -> (subagents running, last seen)


def new_pending() -> dict:
    return {"xs": 0, "xq": 0, "xa": 0, "xc": 0, "ag": 0, "sp": set()}


def take_events(pend: dict) -> None:
    """Claim everything logged so far into `pend`. The rename makes it atomic:
    the hook opens .xp by name on every append, so new events start a fresh
    file."""
    claimed = SESSIONS_DIR / ".xp.taking"
    try:
        XP_LOG.rename(claimed)
    except OSError:
        return
    try:
        lines = claimed.read_text(errors="ignore").splitlines()
    except OSError:
        lines = []
    finally:
        claimed.unlink(missing_ok=True)
    now = time.time()
    for line in lines:
        parts = line.split()
        if len(parts) != 2:
            continue                        # e.g. the old one-letter format
        sid, ev = parts
        if ev in ("A", "a"):                # concurrency counts every agent
            running = _agents.get(sid, (0, now))[0]
            running = running + 1 if ev == "A" else max(0, running - 1)
            _agents[sid] = (running, now)
            for k, (n, seen) in list(_agents.items()):
                if now - seen > AGENT_STALE_SEC:
                    del _agents[k]
            pend["ag"] = max(pend["ag"], sum(n for n, _ in _agents.values()))
        if ev not in XP_EVENTS:
            continue
        if _last_event.get(sid) == ev:      # same event twice in a row: once
            continue
        _last_event[sid] = ev
        pend[XP_EVENTS[ev]] += 1
        if ev == "S":
            project = chat_info(sid)[1]
            if project:
                pend["sp"].add(f"{zlib.crc32(project.encode('utf-8')) & 0xffffffff:08x}")


def pending_fields(pend: dict) -> dict:
    out = {k: pend[k] for k in ("xs", "xq", "xa", "xc") if pend[k]}
    if pend["ag"]:
        out["ag"] = pend["ag"]
    if pend["sp"]:
        out["sp"] = ",".join(sorted(pend["sp"])[:5])
    return out


def pending_any(pend: dict) -> bool:
    return any(pend[k] for k in ("xs", "xq", "xa", "xc", "ag")) or bool(pend["sp"])


def send_partial(data: dict, mode: str, ser, host: str) -> bool:
    """Push a few fields without a usage fetch; the device keeps the rest."""
    if mode == "wifi":
        return send_wifi(data, host)
    if mode == "serial" and ser:
        try:
            ser.write((json.dumps(data, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8"))
            return True
        except Exception:
            return False
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
    org_uuid         = None
    source_plan      = ""
    wifi_check_every = 30
    last_wifi_check  = 0
    use_oauth        = not session_key

    print("Claude Monitor CYD — Bridge")
    print(f"  Source:    {'Claude Code OAuth (Keychain)' if use_oauth else 'claude.ai sessionKey'}")
    print(f"  Interval:  {interval}s")
    print(f"  WiFi:      {wifi_host}")
    print()

    if use_oauth:
        print("Reading Claude Code credentials... ", end="", flush=True)
        try:
            cred        = read_credentials()
            source_plan = plan_from_credentials(cred)
            print(f"OK  plan: {plan or source_plan or 'unknown'}")
        except TokenUnavailableError as ex:
            print(f"FAILED — {ex}")
            print()
            print("  Is Claude Code installed and logged in on this machine?")
            print("  Alternatively run with --session-key to use the old claude.ai path.")
            sys.exit(1)
    else:
        print("Fetching org info from claude.ai... ", end="", flush=True)
        try:
            org_uuid, source_plan = get_org_info(session_key)
            print(f"OK ({org_uuid[:8]}...)  plan: {plan or source_plan or 'unknown'}")
        except SessionExpiredError:
            print("FAILED — SESSION KEY EXPIRED")
            print()
            print("  Get a new key: claude.ai → DevTools → Application → Cookies → sessionKey")
            print("  Then update --session-key in ~/Library/LaunchAgents/com.claude.cyd-bridge.plist")
            print("  Or drop --session-key entirely to use the Claude Code token instead.")
            sys.exit(1)
        except Exception as ex:
            print(f"FAILED: {ex}")
            print("Check your session key (--session-key).")
            sys.exit(1)

    plan_name = plan or source_plan
    pend = new_pending()        # events claimed from the log, not yet delivered

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
                    why = f" — {wifi_host}: {_last_wifi_error}" if _last_wifi_error else ""
                    print(f"\r[WAIT] No CYD found (WiFi or USB){why}  ", end="", flush=True)
                    time.sleep(5)
                    continue

        # Fetch usage
        try:
            usage   = oauth_usage() if use_oauth else session_usage(session_key, org_uuid)
            payload = build_payload(usage, plan_name)
        except TokenUnavailableError as ex:
            # Transient: Claude Code refreshes the token on its own.
            print(f"\r[TOKEN] {ex} — retrying in 60s  ", end="", flush=True)
            time.sleep(60)
            continue
        except SessionExpiredError:
            print("\r[SESSION EXPIRED] Get new sessionKey from claude.ai → update plist → restart bridge  ")
            time.sleep(300)  # check again in 5 min, not every 30s
            continue
        except Exception as ex:
            print(f"\r[ERR]  usage fetch: {ex}  ", end="", flush=True)
            time.sleep(interval)
            continue

        waiting, working = session_scan()
        entries = wait_entries(waiting)
        take_events(pend)
        payload.update({"waiting": len(waiting), "working": working, "wl": wait_list(entries)})
        payload.update(pending_fields(pend))

        # Send
        ok = False
        if mode == "wifi":
            ok = send_wifi(payload, wifi_host)
            if not ok:
                forget_host(wifi_host)
                mode = None
                last_wifi_check = 0
        elif mode == "serial" and ser:
            try:
                line = json.dumps(payload, separators=(",", ":"), ensure_ascii=False) + "\n"
                ser.write(line.encode("utf-8"))
                ok = True
            except Exception:
                ser  = None
                mode = None

        if ok:
            pend = new_pending()
            status = ["OK", "CAUTION", "WARNING", "CRITICAL"][payload["warning_level"]]
            tag    = "[WiFi]" if mode == "wifi" else "[USB] "
            print(
                f"\r{tag} {payload['plan_name']}  "
                f"5H:{payload['fh_pct']}%({payload['fh_reset_min']}m)  "
                f"7D:{payload['sd_pct']}%({payload['sd_reset_min']}m)  "
                + (f"{payload['sc_name']}:{payload['sc_pct']}%  " if payload['sc_name'] else "")
                + (f"waiting:{payload['waiting']}  " if payload['waiting'] else "")
                + f"[{status}]  ",
                end="", flush=True,
            )

        # Sleep until the next usage poll, but push the session counts the
        # moment they change (a waiting session is what people watch the
        # screen for; 60s late is too late), and Claude's XP every 10s.
        last_key = ([(e[0], e[1], e[3]) for e in entries], working)
        last_xp_push = time.time()
        deadline = time.time() + interval
        while time.time() < deadline:
            time.sleep(1)
            waiting, working = session_scan()
            entries = wait_entries(waiting)
            key = ([(e[0], e[1], e[3]) for e in entries], working)   # who waits and why, not how long
            take_events(pend)
            xp_due = pending_any(pend) and time.time() - last_xp_push >= XP_PUSH_SEC
            if (key != last_key or xp_due) and mode is not None:
                data = {"waiting": len(waiting), "working": working, "wl": wait_list(entries)}
                data.update(pending_fields(pend))
                if send_partial(data, mode, ser, wifi_host):
                    last_key, pend, last_xp_push = key, new_pending(), time.time()


def main():
    parser = argparse.ArgumentParser(description="Usage bridge for Claude Monitor CYD")
    parser.add_argument("--session-key", default="",
                        help="Use the old claude.ai path with this sessionKey cookie "
                             "instead of the Claude Code OAuth token")
    parser.add_argument("--plan", default="",
                        help="Override plan name on display (auto-detected if omitted)")
    parser.add_argument("--port", default="",
                        help="USB serial port hint (auto-detected if omitted)")
    parser.add_argument("--interval", type=float, default=60.0,
                        help="Polling interval in seconds (default: 60)")
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
