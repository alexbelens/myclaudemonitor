# Claude Monitor CYD

## Project Overview

A hardware dashboard that displays Claude Code token usage, cost, burn rate, and session timer on an ESP32 "Cheap Yellow Display" (CYD). Currently in **UI development phase** (LVGL PC simulator). Hardware integration pending CYD board arrival.

**Target hardware**: ESP32 CYD (320x240 2.8" LCD touchscreen, ~$15)
**Simulator**: LVGL v9 + SDL2 on desktop (same code runs on both)

## Architecture (3 layers)

```
Python bridge (scripts/) --> JSON file (temp dir) --> C/LVGL UI (src/)
```

1. **Data source**: Python scripts read Claude Code session data from `~/.config/claude/` or generate mock data
2. **Transport**: JSON written to temp file (simulator) or USB serial/BLE (hardware)
3. **Display**: C code with LVGL renders gauges, progress bars, labels at 320x240

## Project Structure

```
claude-monitor-cyd/
├── src/
│   ├── main.c                  # LVGL entry point (replaces lv_port_pc_vscode's main)
│   └── claude_monitor_ui.h     # All dashboard widgets (portable to ESP32)
├── scripts/
│   ├── mock_data.py            # Fake data generator for testing (no deps)
│   └── bridge_serial.py        # Real data bridge (reads ~/.config/claude/)
├── setup.sh                    # Linux/macOS setup
├── setup_windows.ps1           # Windows setup (PowerShell as Admin)
└── README.md
```

## Languages & Tools

- **C** (C99): LVGL UI code in `src/` — targets both desktop simulator and ESP32
- **Python** (3.9+): Bridge scripts in `scripts/` — no external deps for mock_data.py, pyserial optional for bridge_serial.py
- **CMake**: Build system for the LVGL simulator
- **LVGL v9**: Graphics library (progress bars, labels, themes)
- **SDL2**: Desktop simulator backend
- **vcpkg**: Windows dependency manager for SDL2

## Build & Run

### Windows prerequisites
- Git, Python 3.9+, CMake, Visual Studio Build Tools (MSVC C compiler)
- SDL2 via vcpkg

### Quick commands
```powershell
# Setup (PowerShell as Admin)
Set-ExecutionPolicy Bypass -Scope Process
.\setup_windows.ps1

# Mock data (no build needed)
python scripts\mock_data.py --demo

# Simulator (after build)
.\lv_port_pc_vscode\build\bin\Release\main.exe
```

### Linux/macOS
```bash
chmod +x setup.sh && ./setup.sh
```

## Key Data Flow

1. `mock_data.py` or `bridge_serial.py` writes JSON to `%TEMP%\claude_monitor_data.json` (Windows) or `/tmp/claude_monitor_data.json` (Unix)
2. The C code in `claude_monitor_ui.h` reads that file every 2 seconds via `load_data_from_file()`
3. JSON fields: `tokens_used`, `tokens_limit`, `cost_used`, `cost_limit`, `burn_rate`, `depletion_min`, `session_elapsed_min`, `session_total_min`, `plan_name`, `warning_level`

## Cross-Platform Notes

- All file paths use `tempfile.gettempdir()` (Python) and `GetTempPathA()` (C on Windows)
- The C code uses `#ifdef _WIN32` for Windows-specific temp path detection
- Bridge script checks `sys.platform == "win32"` and uses `%APPDATA%\.config\claude\` on Windows

## Plan Limits

| Plan   | Tokens  | Cost    |
|--------|---------|---------|
| Pro    | 19,000  | $18.00  |
| Max5   | 88,000  | $35.00  |
| Max20  | 220,000 | $140.00 |

## Warning Levels

| Level | Threshold | Color  |
|-------|-----------|--------|
| 0     | <50%      | Green  |
| 1     | 50-75%    | Blue   |
| 2     | 75-90%    | Orange |
| 3     | >90%      | Red    |

## Coding Conventions

### C code (src/)
- C99 standard (must compile on ESP32 Arduino toolchain)
- No dynamic allocation in UI code — all LVGL objects created once at init
- Minimal JSON parser built-in (no external deps)
- Keep `claude_monitor_ui.h` portable: no OS-specific headers except behind `#ifdef _WIN32`
- All widget handles are file-static globals (`static lv_obj_t *`)

### Python code (scripts/)
- Standard library only for mock_data.py (zero deps)
- pyserial is the only optional external dep (for bridge_serial.py serial mode)
- Atomic file writes via `os.replace()` to prevent partial reads

## Hardware Roadmap

- [ ] Serial bridge for ESP32 CYD over USB
- [ ] BLE wireless mode
- [ ] Touch interaction (tap to cycle views)
- [ ] Multiple view modes (realtime, daily, monthly)
- [ ] OTA firmware update via WiFi
- [ ] PlatformIO config for CYD board (`esp32dev`, Arduino framework, LVGL + TFT_eSPI)

## Related Projects

- [claude-monitor](https://github.com/Maciek-roboblog/Claude-Code-Usage-Monitor) — the data engine this project extends
- [LVGL](https://lvgl.io/) — graphics library
- [ESP32 CYD Community](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) — hardware docs
