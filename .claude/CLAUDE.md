# Claude Monitor CYD

## Project Overview

Physical desktop dashboard for Claude Code usage monitoring on an ESP32-2432S028 "Cheap Yellow Display". Three-screen touchscreen UI showing real-time metrics from claude-monitor v3.1.

**Hardware**: ESP32 CYD (320x240 2.8" ILI9341 LCD, XPT2046 touch, CH340 USB, WiFi/BT)
**Status**: Working on hardware with USB serial bridge. WiFi mode planned.

## Architecture

```
PC: bridge_combined.py (claude-monitor + win32gui) <--serial--> ESP32: LVGL UI
```

- **Bridge** imports `claude_monitor.data.analysis.analyze_usage()` directly — same engine as terminal
- **P90 limits** via `AdvancedCustomLimitDisplay._calculate_session_percentiles()`
- **Bidirectional serial**: PC sends monitor data + window list, CYD sends switch commands + config changes

## Project Structure

```
claude-monitor-cyd/
├── firmware/                     # ESP32 PlatformIO project
│   ├── platformio.ini
│   ├── src/main.cpp              # Arduino: raw HSPI display + LVGL + touch + serial
│   └── include/
│       ├── lv_conf.h             # LVGL 9.2: 16-bit, 48KB heap, BTN+BAR+LABEL only
│       └── User_Setup.h          # TFT_eSPI CYD pins (HSPI: 13/12/14/15/2)
├── src/
│   ├── main.c                    # PC simulator (SDL2)
│   └── claude_monitor_ui.h       # SHARED UI: 3 screens (Monitor/Windows/Settings)
├── scripts/
│   ├── bridge_combined.py        # Main bridge: monitor + windows + config
│   ├── bridge_live.py            # Monitor-only bridge
│   ├── bridge_serial.py          # Basic serial bridge
│   ├── bridge_claude_monitor.py  # Direct JSONL parser
│   └── mock_data.py              # Mock data (zero deps)
├── setup.sh / setup_windows.ps1  # PC simulator setup
└── TODO.md                       # Full roadmap
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
- Fonts: Montserrat 10 + 12 only
- Widgets enabled: Label, Bar, Btn, Obj
- NEON/Helium .S assembly files must be deleted after `pio run` (ARM-only, breaks Xtensa)

### Serial Protocol
- Baud: 115200
- PC->CYD: `{"type":"monitor",...}\n` and `{"type":"windows","list":[...]}\n`
- CYD->PC: `{"switch":<hwnd>}\n` and `{"config":{"plan":"...","view":"..."}}\n`
- Routing: `route_serial_message()` checks `"type"` field

### Three Screens
1. **Monitor**: cost/tokens/msgs bars, burn rate, cost rate, model, depletion
2. **Windows**: 2x5 button grid of CLI windows, tap to switch (win32gui)
3. **Settings**: plan selector (Custom/Pro/Max5/Max20), view selector (Realtime/Daily/Monthly)

## Build & Run

### Flash ESP32
```powershell
cd firmware
pio run -e cyd --target upload --upload-port COM3
# NOTE: delete .pio/**/**.S files if NEON build errors occur
```

### Run bridge
```powershell
uv tool run --from claude-monitor --with pyserial --with pywin32 python scripts/bridge_combined.py --port COM3
```

### PC simulator
```powershell
.\setup_windows.ps1
python scripts\mock_data.py --demo
.\lv_port_pc_vscode\bin\Release\main.exe
```

## Coding Conventions

### C code (src/, firmware/)
- C99 for UI header (ESP32 Arduino toolchain compatibility)
- No dynamic allocation in UI — all objects created once at init
- `#ifdef ESP32` guards for platform-specific code
- `SERIAL_PRINTF` macro for CYD->PC output (maps to Serial.printf or printf)

### Python code (scripts/)
- Bridge scripts import claude-monitor modules directly via `uv tool run --from claude-monitor`
- Threading for bidirectional serial (listener thread + main loop)
- `BridgeState` class for mutable config shared between threads

## Related Projects

- [claude-monitor](https://github.com/Maciek-roboblog/Claude-Code-Usage-Monitor) — data engine
- [LVGL](https://lvgl.io/) — graphics library
- [ESP32 CYD](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) — hardware docs
