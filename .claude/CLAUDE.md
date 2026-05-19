# Claude Monitor CYD

## Project Overview

Physical desktop dashboard for Claude.ai usage monitoring on an ESP32-2432S028 "Cheap Yellow Display". Two-screen touchscreen UI showing real-time utilization data fetched directly from the Claude.ai API.

**Hardware**: ESP32 CYD (320x240 2.8" ILI9341 LCD, XPT2046 touch, CH340 USB, WiFi/BT)
**Status**: Working on hardware. WiFi mode primary, USB serial fallback.

## Architecture

```
Mac: bridge_auto.py (Claude.ai API) --WiFi/USB--> ESP32: LVGL UI
```

- **Bridge** calls `claude.ai/api/organizations/{uuid}/usage` directly — accurate 5h + 7d utilization %
- **Transport**: WiFi HTTP POST to `claude-monitor.local/api/monitor` (primary), USB serial fallback
- **LaunchAgent**: `~/Library/LaunchAgents/com.claude.cyd-bridge.plist` (session key stored here, outside git)

## Project Structure

```
claude-monitor-cyd/
├── firmware/                     # ESP32 PlatformIO project
│   ├── platformio.ini
│   ├── src/main.cpp              # Arduino: HSPI display + LVGL + touch + WiFi + HTTP server
│   └── include/
│       ├── lv_conf.h             # LVGL 9.2: 16-bit, 48KB heap, BTN+BAR+LABEL only
│       └── User_Setup.h          # TFT_eSPI CYD pins (HSPI: 13/12/14/15/2)
├── src/
│   └── claude_monitor_ui.h       # SHARED UI: 2 screens (Monitor/Settings)
├── scripts/
│   ├── bridge_auto.py            # Bridge: Claude.ai API → CYD display
│   ├── mock_data.py              # Mock data for PC simulator
│   └── setup_wifi.py             # WiFi provisioning helper
├── setup.sh / setup_windows.ps1  # PC simulator setup (SDL2)
└── TODO.md                       # Roadmap
```

## Key Technical Details

### Display Driver
- Raw HSPI SPI (NOT TFT_eSPI — library doesn't work with this CYD variant)
- X-mirror fix in LVGL flush callback (pixels written in reverse column order)
- XY-mirror fix in touch callback (`319 - x`, `239 - y`)
- SPI speed: 20MHz, SPI_MODE0

### LVGL Configuration
- Color depth: 16-bit RGB565
- Heap: 48KB
- Draw buffer: 320 x 20 lines (partial rendering)
- Fonts: Montserrat 10 + 12 + 14 + 28
- Widgets enabled: Label, Bar, Btn, Obj
- NEON/Helium .S assembly files must be deleted after `pio run` (ARM-only, breaks Xtensa)

### Data Model
Bridge sends JSON payload every 30s:
```json
{
  "fh_pct":        42,    // 5-hour utilization % (from Claude.ai API)
  "fh_reset_min":  183,   // minutes until 5h block resets
  "sd_pct":        15,    // 7-day utilization %
  "sd_reset_min":  4320,  // minutes until 7d block resets
  "warning_level": 1,     // 0=ok 1=50% 2=75% 3=90%
  "plan_name":     "Pro"
}
```

### Transport Protocol
- Baud: 115200 (USB serial fallback)
- PC→CYD: `{...}\n` JSON payload
- WiFi: HTTP POST to `http://claude-monitor.local/api/monitor`

### Two Screens
1. **Monitor**: clock/date, weather (wttr.in), 5H bar + countdown, 7D bar, plan name
2. **Settings**: WiFi provisioning (AP mode), timezone ±

### Session Key
- Stored in `~/Library/LaunchAgents/com.claude.cyd-bridge.plist` (outside git, never committed)
- Get from: claude.ai → DevTools → Application → Cookies → `sessionKey`
- Expires in weeks/months. Bridge prints `[SESSION EXPIRED]` in `/tmp/cyd-bridge.log` when it does.

## Build & Run

### Flash ESP32
```bash
cd firmware
# Delete ARM assembly files if build errors occur:
find .pio -name "*.S" -delete
~/.platformio/penv/bin/pio run -e cyd --target upload --upload-port /dev/cu.usbserial-1340
```

### Run bridge manually
```bash
python3 scripts/bridge_auto.py --session-key sk-ant-sid02-...
```

### LaunchAgent (auto-start on login)
```bash
# Restart after plist changes:
launchctl unload ~/Library/LaunchAgents/com.claude.cyd-bridge.plist
launchctl load ~/Library/LaunchAgents/com.claude.cyd-bridge.plist
# Check log:
tail -f /tmp/cyd-bridge.log
```

## Coding Conventions

### C code (src/, firmware/)
- C99 for UI header (ESP32 Arduino toolchain compatibility)
- No dynamic allocation in UI — all objects created once at init
- `#ifdef ESP32` guards for platform-specific code
- `SERIAL_PRINTF` macro for debug output

### Python code (scripts/)
- `curl_cffi` with `impersonate="chrome120"` required to bypass Cloudflare on claude.ai
- `SessionExpiredError` raised on HTTP 401/403 — check for `[SESSION EXPIRED]` in log

## Related Projects

- [LVGL](https://lvgl.io/) — graphics library
- [ESP32 CYD](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) — hardware docs
- [Usage4Claude](https://github.com/f-is-h/Usage4Claude) — macOS menu bar app using same API
