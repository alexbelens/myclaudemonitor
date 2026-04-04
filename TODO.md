# Claude Monitor CYD — Roadmap

## Completed

- [x] LVGL PC simulator (320x240, SDL2, CMake)
- [x] Mock data generator (`scripts/mock_data.py`)
- [x] ESP32 CYD firmware with raw HSPI display driver
- [x] ILI9341 display working (X-mirror fix in flush callback)
- [x] XPT2046 touch working (XY-mirror fix in touch callback)
- [x] LVGL rendering on real hardware
- [x] USB serial bridge with claude-monitor v3.1 integration (`scripts/bridge_live.py`)
- [x] P90 dynamic limits (cost, tokens, messages) matching terminal view
- [x] Dashboard UI: cost, tokens, messages bars + burn rate, cost rate, model, reset time
- [x] Dual-screen UI: Monitor dashboard + Window Switcher
- [x] Tab bar navigation between screens
- [x] Combined bridge (`scripts/bridge_combined.py`) with monitor data + window enumeration
- [x] Window switch commands (CYD → PC via serial)
- [x] PlatformIO project structure (`firmware/`)

## In Progress

- [ ] Touch calibration fine-tuning (XY mirror confirmed working, precision TBD)

## Next: Settings Screen (Plan + View Selection)

Add a third screen to the CYD for changing plan and view mode directly from the touchscreen. Settings persist in ESP32 NVS flash and are sent to the bridge on connect.

### Screen Layout (320x240)

```
┌──────────────────────────────────┐
│  Settings                        │  header (26px)
├──────────────────────────────────┤
│                                  │
│  Plan:                           │
│  ┌──────┐┌──────┐┌──────┐┌────┐ │
│  │Custom││ Pro  ││ Max5 ││Max20│ │  plan selector (44px)
│  └──────┘└──────┘└──────┘└────┘ │
│                                  │
│  View:                           │
│  ┌──────────┐┌───────┐┌────────┐│
│  │ Realtime ││ Daily ││Monthly ││  view selector (44px)
│  └──────────┘└───────┘└────────┘│
│                                  │
│  Active: Custom (P90) | Realtime │  status line
│                                  │
├──────────────────────────────────┤
│ [Monitor]  [Windows]  [Settings] │  tab bar (24px)
└──────────────────────────────────┘
```

### Firmware Changes

- [ ] Add third screen (`scr_settings`) to `claude_monitor_ui.h`
- [ ] Update tab bar to 3 tabs: Monitor | Windows | Settings
- [ ] Plan selector: 4 toggle buttons (Custom/Pro/Max5/Max20)
  - Active plan highlighted with accent color
  - Tapping a plan sends `{"config":{"plan":"max5"}}` to bridge via serial
- [ ] View selector: 3 toggle buttons (Realtime/Daily/Monthly)
  - Active view highlighted
  - Tapping sends `{"config":{"view":"daily"}}` to bridge
- [ ] Store selected plan + view in ESP32 NVS via `Preferences` library
  - Persists across reboots
  - On boot, send saved config to bridge automatically
- [ ] Status line showing current active plan and view mode
- [ ] Enable `LV_USE_BTNMATRIX` in lv_conf.h (efficient button group widget)

### Bridge Changes

- [ ] Listen for `{"config":{...}}` messages from CYD
- [ ] When plan changes: re-run `analyze_usage()` with new plan parameter
- [ ] When view changes: switch between realtime/daily/monthly data format
  - Realtime: current active block (existing)
  - Daily: `UsageAggregator` with `aggregation_mode="daily"`
  - Monthly: `UsageAggregator` with `aggregation_mode="monthly"`
- [ ] Send acknowledgment `{"config_ack":{"plan":"max5","view":"realtime"}}` back to CYD
- [ ] On connect, request config from CYD: `{"get_config":true}`

### Protocol

```
CYD → PC:  {"config":{"plan":"max5"}}\n           (plan change)
CYD → PC:  {"config":{"view":"daily"}}\n          (view change)
CYD → PC:  {"config":{"plan":"pro","view":"monthly"}}\n  (both)
PC → CYD:  {"config_ack":{"plan":"max5","view":"realtime"}}\n
PC → CYD:  {"get_config":true}\n                   (on connect)
CYD → PC:  {"config":{"plan":"custom","view":"realtime"}}\n (response)
```

### Testing

- [ ] Tap plan buttons → bridge switches limits, CYD updates display
- [ ] Tap view buttons → bridge sends daily/monthly data, CYD renders new format
- [ ] Reboot CYD → settings persist, bridge picks them up on reconnect
- [ ] Settings screen works alongside Monitor and Windows screens

---

## Next: WiFi Connection

Replace USB serial data link with WiFi while keeping USB as fallback.

### Firmware Changes (`firmware/src/main.cpp`)

- [ ] Add `WiFi.h` and `WebServer.h` includes
- [ ] Add WiFi credentials storage via `Preferences` library (NVS flash)
- [ ] WiFi connection on startup with timeout (5s)
- [ ] Auto-detect: if WiFi connects → HTTP mode, else → USB serial mode (current code)
- [ ] HTTP server on CYD (port 80):
  - `POST /api/monitor` — receives monitor JSON payload
  - `POST /api/windows` — receives window list JSON payload
  - `GET /api/status` — returns CYD status (uptime, WiFi RSSI, current screen)
- [ ] HTTP client for switch commands:
  - `POST http://<PC_IP>:8080/switch` — sends `{"switch": <hwnd>}` to PC
- [ ] mDNS responder: CYD advertises as `claude-monitor.local`
- [ ] Display WiFi status icon in header (connected/disconnected/signal strength)
- [ ] First-boot WiFi config mode: if no saved credentials, show AP portal for setup

### Bridge Changes (`scripts/bridge_combined.py`)

- [ ] Add HTTP POST mode: send monitor data to `http://claude-monitor.local/api/monitor`
- [ ] Add HTTP POST mode: send window list to `http://claude-monitor.local/api/windows`
- [ ] Add HTTP server on PC (port 8080): listen for `/switch` commands from CYD
- [ ] Auto-detect: try HTTP first (mDNS lookup), fall back to serial
- [ ] mDNS discovery of CYD (`claude-monitor.local`)
- [ ] Connection status logging (WiFi vs Serial mode)

### Configuration

- [ ] WiFi SSID + password stored in ESP32 NVS (persists across reboots)
- [ ] PC IP address: either hardcoded, mDNS (`<hostname>.local`), or broadcast discovery
- [ ] Add `--wifi` / `--serial` / `--auto` flags to bridge script

### Testing

- [ ] CYD boots, connects to WiFi, shows dashboard via HTTP
- [ ] Window switching works over WiFi
- [ ] Unplug USB data (power from charger) — still works over WiFi
- [ ] WiFi drops — falls back to USB serial gracefully
- [ ] WiFi reconnects — switches back to WiFi mode

## Future Ideas

### Bluetooth LE Mode
- [ ] BLE serial profile as alternative to WiFi
- [ ] Direct PC-to-CYD pairing without router dependency
- [ ] Useful as fallback when WiFi is unavailable

### Touch Calibration Screen
- [ ] Display crosshairs at known positions
- [ ] Record raw XPT2046 values
- [ ] Compute and save calibration coefficients to NVS

### Additional Display Views
- [ ] Daily usage summary (matches `claude-monitor --view daily`)
- [ ] Monthly cost tracking (matches `claude-monitor --view monthly`)
- [ ] Session history timeline
- [ ] Swipe gestures to navigate between views

### Hardware Polish
- [ ] 3D-printed desk stand / case
- [ ] OTA firmware updates via WiFi (no USB needed to update)
- [ ] Deep sleep mode when idle (wake on touch)
- [ ] RGB LED status indicator (back of board)
- [ ] Brightness control via touch (dim at night)

### Integration
- [ ] Plugin for claude-monitor (`claude-monitor --output cyd`)
- [ ] Publish as open-source companion to claude-monitor repo
- [ ] PlatformIO library registry listing
- [ ] Pre-built firmware binaries for easy flashing (no dev env needed)
