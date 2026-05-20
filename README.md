# Claude Monitor CYD

A physical desktop dashboard for Claude.ai usage — built on an ESP32 "Cheap Yellow Display". Shows real utilization data fetched directly from the Claude.ai API, displayed on a 2.8" touchscreen you can place on your desk.

**Hardware**: ESP32-2432S028 (~$15) | **Display**: 320×240 ILI9341 touchscreen | **Connection**: WiFi (USB serial fallback)

![Monitor screen showing 5H and 7D utilization bars](docs/screen.jpg)

## What it shows

- **5H bar** — current 5-hour block utilization % with real-time countdown to reset
- **7D bar** — 7-day rolling window utilization %
- **Weather** — current conditions and temperature (Open-Meteo ICON-EU model, refreshes every 3 min)
- **Plan name** — Pro / Max5 / Max20

## Settings screen

- **WiFi** — connect to new network via captive portal (CYD-Setup AP)
- **Device #** — unique number per device; `1` → `claude-monitor.local`, `2` → `claude-monitor-2.local` (reboots to apply)
- **Timezone** — UTC offset ±, saves to flash
- **Sleep/Wake** — backlight schedule by hour; touch the screen to wake early

Data comes directly from `claude.ai/api/organizations/{uuid}/usage` — the same numbers you see on the Claude.ai website.

## Requirements

- ESP32-2432S028 CYD board
- USB-C cable (first flash only; OTA after that)
- [PlatformIO](https://platformio.org/)
- Python 3.10+ with `curl-cffi` and `pyserial`
- A Claude.ai account (Pro or Max)

## Setup

### 1. Flash the firmware

```bash
cd firmware
# If build fails with ARM assembly errors:
find .pio -name "*.S" -delete
~/.platformio/penv/bin/pio run -e cyd --target upload --upload-port /dev/cu.usbserial-XXXX
```

On first boot the CYD starts a WiFi AP named **CYD-Setup**. Connect to it, open `192.168.4.1`, enter your WiFi credentials.

### 2. Get your session key

1. Open **claude.ai** in your browser (logged in)
2. DevTools → **Application** → **Cookies** → `https://claude.ai`
3. Copy the value of **`sessionKey`** (starts with `sk-ant-sid02-...`)

### 3. Run the bridge

```bash
pip install curl-cffi pyserial
python3 scripts/bridge_auto.py --session-key sk-ant-sid02-...
```

The bridge auto-detects the CYD — WiFi first (`claude-monitor.local`), USB serial fallback.

### 4. Auto-start on login (macOS)

Create `~/Library/LaunchAgents/com.claude.cyd-bridge.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.claude.cyd-bridge</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/bin/python3</string>
        <string>/path/to/scripts/bridge_auto.py</string>
        <string>--session-key</string>
        <string>sk-ant-sid02-YOUR-KEY-HERE</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>/tmp/cyd-bridge.log</string>
    <key>StandardErrorPath</key>
    <string>/tmp/cyd-bridge.log</string>
</dict>
</plist>
```

```bash
launchctl load ~/Library/LaunchAgents/com.claude.cyd-bridge.plist
tail -f /tmp/cyd-bridge.log
```

## OTA firmware updates

After the first USB flash, all future updates go over WiFi — no cable needed:

```bash
cd firmware
find .pio -name "*.S" -delete
~/.platformio/penv/bin/pio run -e cyd_ota --target upload
```

The `cyd_ota` environment targets `claude-monitor.local` by default. For device #2 pass `--upload-port claude-monitor-2.local`.

## Multiple devices

Each CYD can be assigned a unique number in Settings → **Device #**:

| Setting | mDNS hostname | Bridge flag |
|---------|--------------|-------------|
| 1 (default) | `claude-monitor.local` | *(none)* |
| 2 | `claude-monitor-2.local` | `--host claude-monitor-2.local` |
| 3 | `claude-monitor-3.local` | `--host claude-monitor-3.local` |

Run a separate bridge process for each device:

```bash
# Device 1 (default)
python3 scripts/bridge_auto.py --session-key sk-ant-...

# Device 2
python3 scripts/bridge_auto.py --session-key sk-ant-... --host claude-monitor-2.local
```

## Session key expiry

The session key lasts weeks to months. When it expires the bridge logs:

```
[SESSION EXPIRED] Get new sessionKey from claude.ai → update plist → restart bridge
```

Get a new key from the browser, update the plist, then:
```bash
launchctl unload ~/Library/LaunchAgents/com.claude.cyd-bridge.plist
launchctl load  ~/Library/LaunchAgents/com.claude.cyd-bridge.plist
```

## Architecture

```
Mac: bridge_auto.py
  └─ curl_cffi → claude.ai/api/organizations/{uuid}/usage
  └─ HTTP POST → claude-monitor.local/api/monitor (WiFi)
               → /dev/cu.usbserial-* at 115200 (USB fallback)

ESP32 CYD:
  ├─ HTTP server (port 80) — receives JSON payload
  ├─ ArduinoOTA — WiFi firmware updates
  ├─ LVGL UI — 5H bar, 7D bar, weather, plan name
  └─ Settings — WiFi, device #, timezone, sleep/wake schedule
```

## Project Structure

```
claude-monitor-cyd/
├── firmware/
│   ├── platformio.ini            # cyd (USB) + cyd_ota (WiFi OTA) environments
│   ├── src/main.cpp              # ESP32: display, touch, WiFi, HTTP, OTA, weather, sleep
│   └── include/
│       ├── lv_conf.h             # LVGL 9.2 config
│       └── User_Setup.h          # CYD pin mappings
├── src/
│   └── claude_monitor_ui.h       # LVGL UI — Monitor + Settings screens
├── scripts/
│   ├── bridge_auto.py            # Bridge: Claude.ai API → CYD
│   └── mock_data.py              # Fake data for simulator testing
└── setup.sh / setup_windows.ps1  # PC simulator setup (SDL2)
```

## Hardware Notes

- **Board**: ESP32-2432S028 (ILI9341, XPT2046 touch, CH340 USB-serial)
- **Display driver**: raw HSPI SPI — TFT_eSPI doesn't work with this board variant
- **Orientation**: portrait 240×320, 180° rotation
- **X-mirror fix**: pixels written in reverse column order in LVGL flush callback
- **Touch fix**: `319 - x`, `239 - y` in touch callback
- **Backlight**: GPIO 21 — LOW = off (sleep), HIGH = on

## Credits

- [LVGL](https://lvgl.io/) — graphics library
- [ESP32 CYD](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) — hardware docs
- [Usage4Claude](https://github.com/f-is-h/Usage4Claude) — inspiration for using the Claude.ai API directly
- [Open-Meteo](https://open-meteo.com/) — weather data (ICON-EU model, free, no API key)

## License

MIT
