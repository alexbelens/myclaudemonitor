# TODO — Claude Monitor CYD

## Done ✓

- ESP32 CYD display driver (raw HSPI, no TFT_eSPI)
- LVGL 9.2 portrait UI (240×320)
- WiFi provisioning (AP captive portal)
- Timezone setting (NVS persistence)
- Weather via wttr.in (3-min refresh)
- Two-screen UI: Monitor + Settings
- Claude.ai API bridge (5h + 7d utilization, accurate from Anthropic)
- OAuth source: reads the Claude Code token from Keychain (self-refreshing,
  no sessionKey to paste) and polls `/api/oauth/usage` — plain GET, no tokens spent
- Per-model weekly cap on screen (Fable/Opus), hidden when the plan has none
- Binding limit highlighted — the one that will actually stop you
- WiFi transport (claude-monitor.local) + USB serial fallback
- LaunchAgent auto-start on login
- Session key expiry detection (`[SESSION EXPIRED]` in log)
- ArduinoOTA — WiFi flash via `pio run -e cyd_ota --target upload`
  (needs the two-slot `min_spiffs.csv` partition table — with `huge_app.csv`
  there is no second app slot and OTA fails at the first byte)
- Sleep/wake schedule — backlight off/on by hour, configurable in Settings screen, NVS persistence, touch-to-wake

## Pending

- **Air-raid alert stripe** — accent bar (y=70..73) reserved for alerts.in.ua API.
  Token requested, up to 7 days. When received: bridge fetches Kharkiv oblast status
  every 60s, adds `alert_active` bool to payload, UI sets stripe green (safe) / red (alert).

## Ideas

- Show opus/sonnet 7-day model quotas if plan upgrades to Max
- Auto-notify on session key expiry (macOS notification via `osascript`)
