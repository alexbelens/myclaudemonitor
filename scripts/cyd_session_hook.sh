#!/bin/sh
# Claude Code hook: records whether this session is waiting for you, so the
# CYD pet can wave, and logs Claude's actions, which are the pet's XP.
# One small file per session in ~/.claude/cyd-sessions, plus an append-only
# .xp log of one letter per action; bridge_auto.py reads both and pushes the
# waiting count and the XP to the device.
#
#   cyd_session_hook.sh reply       Stop, StopFailure: finished, your turn
#   cyd_session_hook.sh permission  Notification permission_prompt
#   cyd_session_hook.sh question    Notification elicitation_dialog|agent_needs_input
#   cyd_session_hook.sh busy        UserPromptSubmit, PostToolUse, PostToolUseFailure
#   cyd_session_hook.sh end         SessionEnd
#
# The kind comes from the hook's matcher in settings.json rather than from a
# field in the hook's JSON, so the file always says what the session wants.
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
head=$(printf '%s' "$input" | head -c 4096 | tr -d '\n')
field() {   # first "key":"value" in the JSON head; value limited to [A-Za-z0-9_-]
    printf '%s' "$head" | grep -o "\"$1\"[[:space:]]*:[[:space:]]*\"[A-Za-z0-9_-]*\"" | head -n 1 \
        | sed 's/.*"\([A-Za-z0-9_-]*\)"$/\1/'
}
sid=$(field session_id)
[ -n "$sid" ] || exit 0

mkdir -p "$dir" 2>/dev/null || exit 0
# XP log: one letter per action, weighed by the bridge.
#   s = finished an answer (Stop)   e = edited/created a file   t = any other tool
# Failed tool calls and your own prompts earn nothing.
case "$(field hook_event_name)" in
    Stop) xp=s ;;
    PostToolUse)
        case "$(field tool_name)" in
            Edit|Write|MultiEdit|NotebookEdit) xp=e ;;
            *) xp=t ;;
        esac ;;
    *) xp= ;;
esac
[ -n "$xp" ] && printf '%s' "$xp" >> "$dir/.xp" 2>/dev/null

case "$state" in
    waiting) line="waiting reply" ;;          # older settings.json form
    reply|permission|question) line="waiting $state" ;;
    busy) line="busy" ;;
    *) line= ;;
esac
case "$state" in
    end)
        rm -f "$dir/$sid" 2>/dev/null
        ;;
    *)
        [ -n "$line" ] && printf '%s\n' "$line" > "$dir/$sid.tmp" 2>/dev/null \
            && mv -f "$dir/$sid.tmp" "$dir/$sid" 2>/dev/null
        ;;
esac
exit 0
