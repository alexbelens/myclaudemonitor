# Claude Monitor CYD

A physical desktop dashboard that displays real-time Claude Code usage on an ESP32 "Cheap Yellow Display" (CYD). Shows the same data as `claude-monitor --view realtime` on a touchscreen you can place on your desk.

**Hardware**: ESP32-2432S028 (~$15) | **Display**: 320x240 ILI9341 touchscreen | **Connection**: USB serial (WiFi planned)

## Features

- **Real-time monitoring** — cost, tokens, messages usage with P90 dynamic limits
- **Burn rate + cost rate** — live tokens/min and $/min
- **Model distribution** — Opus/Sonnet/Haiku percentage
- **Session timer** — time to reset countdown
- **Depletion predictions** — when tokens will run out
- **CLI window switcher** — tap to bring terminal windows to front (Win32)
- **Settings screen** — change plan (Custom/Pro/Max5/Max20) and view (Realtime/Daily/Monthly) from the touchscreen
- **Three-screen UI** — swipe between Monitor, Windows, and Settings via tab bar

## Quick Start (Hardware)

### Prerequisites

- ESP32-2432S028 CYD board ([$15 on AliExpress](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display))
- USB-C cable
- [PlatformIO](https://platformio.org/) (`pip install platformio`)
- [claude-monitor](https://github.com/Maciek-roboblog/Claude-Code-Usage-Monitor) v3.1+ (`uv tool install claude-monitor`)
- `pyserial` and `pywin32` (for the bridge script)

### 1. Flash the firmware

```powershell
cd firmware
pio run -e cyd --target upload --upload-port COM3
```

### 2. Run the bridge

```powershell
uv tool run --from claude-monitor --with pyserial --with pywin32 python scripts/bridge_combined.py --port COM3
```

The CYD shows your live Claude Code usage. Tap the tab bar to switch between Monitor, Windows, and Settings.

## Quick Start (PC Simulator)

No hardware needed — develop and preview on desktop.

### Windows

```powershell
Set-ExecutionPolicy Bypass -Scope Process
.\setup_windows.ps1
python scripts\mock_data.py --demo        # Terminal 1
.\lv_port_pc_vscode\bin\Release\main.exe  # Terminal 2
```

### macOS / Linux

```bash
chmod +x setup.sh && ./setup.sh
python3 scripts/mock_data.py --demo &
./lv_port_pc_vscode/build/bin/main
```

## Project Structure

```
claude-monitor-cyd/
├── firmware/                     # ESP32 PlatformIO project
│   ├── platformio.ini            # Board config + libraries
│   ├── src/main.cpp              # Arduino entry point (display, touch, serial)
│   └── include/
│       ├── lv_conf.h             # LVGL config (16-bit, 48KB heap, minimal)
│       └── User_Setup.h          # TFT_eSPI CYD pin mappings
├── src/
│   ├── main.c                    # PC simulator entry point (SDL2)
│   └── claude_monitor_ui.h       # Shared UI code (runs on both PC + ESP32)
├── scripts/
│   ├── bridge_combined.py        # Main bridge: monitor + windows + settings
│   ├── bridge_live.py            # Monitor-only bridge (simpler)
│   ├── bridge_serial.py          # Basic serial bridge
│   ├── bridge_claude_monitor.py  # Direct JSONL parser bridge
│   └── mock_data.py              # Fake data generator (no deps)
├── setup.sh                      # Linux/macOS simulator setup
├── setup_windows.ps1             # Windows simulator setup
├── TODO.md                       # Roadmap
└── .claude/CLAUDE.md             # Project instructions for Claude Code
```

## Architecture

```
PC (bridge_combined.py)                      CYD (ESP32)
┌───────────────────────────┐               ┌──────────────────────┐
│ claude-monitor v3.1       │               │ Screen 1: Monitor    │
│   analyze_usage()         │──── JSON ────>│   cost, tokens, msgs │
│   P90 limits              │   (serial)    │   burn rate, model   │
│                           │               ├──────────────────────┤
│ win32gui.EnumWindows()    │──── JSON ────>│ Screen 2: Windows    │
│                           │               │   tap to switch      │
│ SetForegroundWindow()     │<── switch ────│                      │
│                           │               ├──────────────────────┤
│ plan/view config handler  │<── config ────│ Screen 3: Settings   │
│                           │               │   plan + view select │
└───────────────────────────┘               └──────────────────────┘
```

## Serial Protocol

| Direction | Message | Purpose |
|-----------|---------|---------|
| PC -> CYD | `{"type":"monitor","tokens_used":...}\n` | Dashboard metrics |
| PC -> CYD | `{"type":"windows","list":[...]}\n` | CLI window list |
| CYD -> PC | `{"switch":12345}\n` | Bring window to front |
| CYD -> PC | `{"config":{"plan":"pro","view":"realtime"}}\n` | Change settings |

## Bridge Scripts

| Script | Use Case | Dependencies |
|--------|----------|-------------|
| `bridge_combined.py` | Full featured: monitor + windows + settings | claude-monitor, pyserial, pywin32 |
| `bridge_live.py` | Monitor only (no window switching) | claude-monitor, pyserial |
| `mock_data.py` | Testing without real Claude data | none |

## Hardware Notes

- **Board**: ESP32-2432S028 (ILI9341 display, XPT2046 touch, CH340 USB)
- **Display driver**: Raw HSPI SPI (TFT_eSPI incompatible with this board variant)
- **Display fix**: X-mirror in flush callback, XY-mirror in touch callback
- **Resolution**: 320x240 landscape (rotation 1)
- **SPI speed**: 20MHz
- **Power**: USB only (no battery)

## Credits

- [claude-monitor](https://github.com/Maciek-roboblog/Claude-Code-Usage-Monitor) — the data engine (P90 limits, session analysis, cost calculation)
- [LVGL](https://lvgl.io/) — graphics library
- [ESP32 CYD Community](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) — hardware docs

## License

MIT
