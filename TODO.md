# TODO — Claude Monitor CYD

## Done ✓

- ESP32 CYD display driver (raw HSPI, no TFT_eSPI)
- LVGL 9.2 portrait UI (240×320)
- WiFi provisioning (AP captive portal)
- Timezone setting (NVS persistence)
- Weather via wttr.in (3-min refresh)
- Two-screen UI: Monitor + Settings
- Claude.ai API bridge (5h + 7d utilization, accurate from Anthropic)
- WiFi transport (claude-monitor.local) + USB serial fallback
- LaunchAgent auto-start on login
- Session key expiry detection (`[SESSION EXPIRED]` in log)

## Pending

- **Air-raid alert stripe** — accent bar (y=70..73) reserved for alerts.in.ua API.
  Token requested, up to 7 days. When received: bridge fetches Kharkiv oblast status
  every 60s, adds `alert_active` bool to payload, UI sets stripe green (safe) / red (alert).

## Ideas

- Show opus/sonnet 7-day model quotas if plan upgrades to Max
- Auto-notify on session key expiry (macOS notification via `osascript`)
