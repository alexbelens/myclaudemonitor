#!/bin/sh
# Claude Code hook: records whether this session is waiting for you, so the
# CYD pet can wave. One small file per session in ~/.claude/cyd-sessions;
# bridge_auto.py counts the "waiting" ones and pushes the number to the device.
#
#   cyd_session_hook.sh waiting   Stop, StopFailure, Notification (permission/idle)
#   cyd_session_hook.sh busy      UserPromptSubmit, PostToolUse, PostToolUseFailure
#   cyd_session_hook.sh end       SessionEnd
#
# It runs inside every Claude Code session, so it must never get in the way:
# no network, no output, always exit 0. Synchronous on purpose — async hooks
# can land out of order, and a "busy" overwriting a later "waiting" would
# leave the pet silent while a session sits there waiting for you.

state="$1"
dir="$HOME/.claude/cyd-sessions"

# Drain stdin fully (PostToolUse can carry a large tool response), then take
# the first session_id. Only [A-Za-z0-9_-] passes, so it's safe as a filename.
input=$(cat 2>/dev/null)
sid=$(printf '%s' "$input" | head -c 4096 | tr -d '\n' \
      | grep -o '"session_id"[[:space:]]*:[[:space:]]*"[A-Za-z0-9_-]*"' | head -n 1 \
      | sed 's/.*"\([A-Za-z0-9_-]*\)"$/\1/')
[ -n "$sid" ] || exit 0

mkdir -p "$dir" 2>/dev/null || exit 0
case "$state" in
    waiting|busy)
        printf '%s\n' "$state" > "$dir/$sid.tmp" 2>/dev/null && mv -f "$dir/$sid.tmp" "$dir/$sid" 2>/dev/null
        ;;
    end)
        rm -f "$dir/$sid" 2>/dev/null
        ;;
esac
exit 0
