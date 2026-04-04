# Claude Monitor CYD — Physical Dashboard for Claude Code Usage

A hardware dashboard that displays your Claude Code token usage, cost, burn rate, and session timer on an ESP32 "Cheap Yellow Display" (CYD).

**Status**: UI development phase (simulator). Hardware integration when CYD arrives.

```
┌──────────────────────────────────┐
│ ● CLAUDE MONITOR   Pro   2:07:12│
├──────────────────────────────────┤
│  Tokens  ████████░░░░  67%      │
│  12,450 / 19,000 tokens         │
├──────────────────────────────────┤
│  Cost    ████░░░░░░░  $4.23     │
│  $4.23 / $18.00                 │
├──────────────────────────────────┤
│  Burn: 45 t/m      Depl: 2h 12m│
├──────────────────────────────────┤
│  Session ████████████░░░  78%   │
│  Remaining: 1h 06m              │
├──────────────────────────────────┤
│  claude-monitor v3.1 | ESP32-CYD│
└──────────────────────────────────┘
```

## Quick Start

### Option A: Windows (Native PowerShell)

**Prerequisites**: Git, Python 3.9+ (install from python.org or `winget install Python.Python.3.12`)

**Step 1** — Open PowerShell as Administrator and run:

```powershell
# Allow script execution for this session
Set-ExecutionPolicy Bypass -Scope Process

# Unzip the project (or use 7-Zip / WinRAR)
cd C:\Users\YourName\Projects   # or wherever you want it
# Extract claude-monitor-cyd.tar.gz here

cd claude-monitor-cyd
.\setup_windows.ps1
```

The script will auto-install CMake, vcpkg, SDL2, clone the LVGL simulator, and build everything.

**Step 2** — Run mock data (PowerShell terminal 1):

```powershell
python scripts\mock_data.py --demo
```

**Step 3** — Run the simulator (PowerShell terminal 2):

```powershell
# The setup script prints the exact path, but typically:
.\lv_port_pc_vscode\build\bin\Release\main.exe
```

A 320×240 window opens with the live dashboard.

### Option B: WSL2 (Linux on Windows — easiest if you already have it)

If you have WSL2 installed (`wsl --install` in PowerShell), everything works like Linux:

```bash
wsl
cd /mnt/c/Users/YourName/Projects/claude-monitor-cyd
chmod +x setup.sh
./setup.sh
```

Then follow the Linux instructions below.

### Option C: macOS / Linux

```bash
cd claude-monitor-cyd
chmod +x setup.sh
./setup.sh
```

## Project Structure

```
claude-monitor-cyd/
├── setup.sh                    # One-command setup
├── README.md
├── src/
│   ├── main.c                  # LVGL entry point
│   └── claude_monitor_ui.h     # All dashboard widgets (portable)
├── scripts/
│   ├── mock_data.py            # Fake data generator for testing
│   └── bridge_serial.py        # Real data bridge (file or serial)
└── data/                       # (reserved for future config)
```

## Architecture

```
┌─────────────────── Your Machine ───────────────────┐
│                                                     │
│  Claude Code  →  Session logs  →  bridge_serial.py  │
│  (or mock_data.py for testing)                      │
│       │                                             │
│       ▼                                             │
│  /tmp/claude_monitor_data.json   (simulator mode)   │
│       │                          OR                 │
│  USB serial / BLE                (hardware mode)    │
│                                                     │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌──────────── ESP32 CYD (or Simulator) ──────────────┐
│                                                     │
│  Parse JSON  →  Update LVGL widgets  →  LCD output  │
│                                                     │
│  claude_monitor_ui.h  (same code runs on both)      │
└─────────────────────────────────────────────────────┘
```

## Adapting for Real Hardware

When your ESP32 CYD arrives, the UI code (`claude_monitor_ui.h`) transfers directly.
You only need to change the data source:

### In `claude_monitor_ui.h`, replace `load_data_from_file()` with:

```c
// Read JSON from UART serial instead of file
static void load_data_from_serial(void) {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        // Parse the same JSON format
        parse_json(line.c_str());
    }
}
```

### Wiring

| Connection | Detail |
|---|---|
| **USB-C** | ESP32 CYD plugs directly into your Mac/PC USB |
| **Serial** | Python bridge writes JSON at 115200 baud |
| **BLE** | Optional: add BLE serial profile for wireless |

### PlatformIO config for CYD

```ini
[env:cyd]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    lvgl/lvgl@^9.3.0
    bodmer/TFT_eSPI@^2.5.43
```

## Data Format

The JSON payload exchanged between bridge and display:

```json
{
  "tokens_used": 8432,
  "tokens_limit": 19000,
  "cost_used": 7.65,
  "cost_limit": 18.00,
  "burn_rate": 38,
  "depletion_min": 162,
  "session_elapsed_min": 127,
  "session_total_min": 300,
  "plan_name": "Pro",
  "warning_level": 1
}
```

### Warning levels

| Level | Meaning | LED Color |
|-------|---------|-----------|
| 0 | OK (<50%) | Green |
| 1 | Caution (50-75%) | Blue |
| 2 | Warning (75-90%) | Orange |
| 3 | Critical (>90%) | Red |

## Manual Setup

### Windows (if PowerShell script fails)

```powershell
# 1. Install tools
winget install --id Git.Git
winget install --id Kitware.CMake
winget install --id Python.Python.3.12

# 2. Install vcpkg + SDL2
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat -disableMetrics
.\vcpkg\vcpkg install sdl2:x64-windows

# 3. Clone LVGL simulator
git clone --recursive https://github.com/lvgl/lv_port_pc_vscode

# 4. Copy our files into the simulator
copy src\main.c lv_port_pc_vscode\main\src\main.c
copy src\claude_monitor_ui.h lv_port_pc_vscode\main\src\

# 5. Build with vcpkg toolchain
cd lv_port_pc_vscode
cmake -B build -DCMAKE_TOOLCHAIN_FILE="..\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release

# 6. Generate test data + run
python ..\scripts\mock_data.py --once
.\build\bin\Release\main.exe
```

### macOS / Linux

```bash
# 1. Install dependencies
brew install sdl2 cmake          # macOS
# sudo apt install libsdl2-dev cmake build-essential  # Ubuntu

# 2. Clone LVGL simulator
git clone --recursive https://github.com/lvgl/lv_port_pc_vscode
cd lv_port_pc_vscode

# 3. Copy our files
cp ../src/main.c main/src/main.c
cp ../src/claude_monitor_ui.h main/src/

# 4. Build
cmake -B build
cmake --build build -j$(nproc)

# 5. Generate test data + run
python3 ../scripts/mock_data.py --once
./build/bin/main
```

## Roadmap

- [x] LVGL dashboard UI (320×240)
- [x] Mock data generator
- [x] Real claude-monitor bridge (file mode)
- [ ] Serial bridge for ESP32 hardware
- [ ] BLE wireless mode
- [ ] Touch interaction (tap to cycle views)
- [ ] Multiple view modes (realtime / daily / monthly)
- [ ] OTA firmware update via WiFi
- [ ] 3D-printed case design
- [ ] Integration as claude-monitor plugin

## Credits

- [claude-monitor](https://github.com/Maciek-roboblog/Claude-Code-Usage-Monitor) — the data engine
- [LVGL](https://lvgl.io/) — graphics library
- [ESP32 CYD Community](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) — hardware docs

## License

MIT
