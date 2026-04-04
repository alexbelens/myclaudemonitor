#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# Claude Monitor CYD — LVGL Simulator Setup
# ============================================================
# This script sets up an LVGL v9 PC simulator (SDL2 backend)
# so you can develop and preview the Claude Monitor dashboard
# at 320×240 before your ESP32 CYD hardware arrives.
#
# Supported: macOS (Homebrew), Ubuntu/Debian, Fedora
# ============================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
SIM_DIR="$PROJECT_DIR/lv_port_pc_vscode"

# ----------------------------------------------------------
# 1. Install system dependencies
# ----------------------------------------------------------
info "Installing system dependencies..."

if [[ "$OSTYPE" == "darwin"* ]]; then
    if ! command -v brew &>/dev/null; then
        error "Homebrew not found. Install from https://brew.sh"
    fi
    brew install sdl2 cmake
elif command -v apt-get &>/dev/null; then
    sudo apt-get update
    sudo apt-get install -y build-essential cmake libsdl2-dev
elif command -v dnf &>/dev/null; then
    sudo dnf install -y gcc gcc-c++ cmake SDL2-devel
else
    error "Unsupported package manager. Install SDL2, CMake, and a C compiler manually."
fi

info "System dependencies OK."

# ----------------------------------------------------------
# 2. Clone the official LVGL PC simulator (VS Code project)
# ----------------------------------------------------------
if [ -d "$SIM_DIR" ]; then
    info "LVGL simulator already cloned. Pulling latest..."
    cd "$SIM_DIR" && git pull && git submodule update --init --recursive
else
    info "Cloning LVGL PC simulator..."
    git clone --recursive https://github.com/lvgl/lv_port_pc_vscode "$SIM_DIR"
fi

# ----------------------------------------------------------
# 3. Patch lv_conf.h for 320×240 CYD resolution
# ----------------------------------------------------------
LV_CONF="$SIM_DIR/lv_conf.h"

info "Patching lv_conf.h for 320×240 CYD resolution..."

# Set horizontal resolution
sed -i.bak 's/#define LV_HOR_RES_MAX.*/#define LV_HOR_RES_MAX 320/' "$LV_CONF" 2>/dev/null || true
sed -i.bak 's/#define LV_VER_RES_MAX.*/#define LV_VER_RES_MAX 240/' "$LV_CONF" 2>/dev/null || true

# Enable label long modes (scroll, wrap)
sed -i.bak 's/#define LV_LABEL_LONG_TXT_HINT.*0/#define LV_LABEL_LONG_TXT_HINT 1/' "$LV_CONF" 2>/dev/null || true

info "lv_conf.h patched."

# ----------------------------------------------------------
# 4. Copy our Claude Monitor source into the simulator
# ----------------------------------------------------------
info "Installing Claude Monitor source..."

cp "$PROJECT_DIR/src/claude_monitor_ui.h" "$SIM_DIR/main/src/" 2>/dev/null || \
    cp "$PROJECT_DIR/src/claude_monitor_ui.h" "$SIM_DIR/main/" 2>/dev/null || true

# Patch main.c to include our UI instead of the default demo
MAIN_C="$SIM_DIR/main/src/main.c"
if [ ! -f "$MAIN_C" ]; then
    MAIN_C="$SIM_DIR/main/main.c"
fi

if [ -f "$MAIN_C" ]; then
    info "Patching main.c to load Claude Monitor UI..."
    cp "$MAIN_C" "${MAIN_C}.original"
    cp "$PROJECT_DIR/src/main.c" "$MAIN_C"
    info "main.c replaced with Claude Monitor entry point."
else
    warn "Could not find main.c — you may need to manually copy src/main.c"
fi

# Also copy the UI header alongside main.c
cp "$PROJECT_DIR/src/claude_monitor_ui.h" "$(dirname "$MAIN_C")/" 2>/dev/null || true

# ----------------------------------------------------------
# 5. Patch CMakeLists to set 320×240 window
# ----------------------------------------------------------
CMAKE_FILE="$SIM_DIR/CMakeLists.txt"
if [ -f "$CMAKE_FILE" ]; then
    # Try to set SDL window size via compile definitions
    if ! grep -q "SDL_HOR_RES" "$CMAKE_FILE"; then
        echo '' >> "$CMAKE_FILE"
        echo '# Claude Monitor: CYD resolution' >> "$CMAKE_FILE"
        echo 'add_compile_definitions(SDL_HOR_RES=320 SDL_VER_RES=240)' >> "$CMAKE_FILE"
    fi
fi

# ----------------------------------------------------------
# 6. Build
# ----------------------------------------------------------
info "Building the simulator..."
cd "$SIM_DIR"
cmake -B build
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# ----------------------------------------------------------
# 7. Generate initial mock data
# ----------------------------------------------------------
info "Generating mock data..."
python3 "$PROJECT_DIR/scripts/mock_data.py" --once

# ----------------------------------------------------------
# Done
# ----------------------------------------------------------
echo ""
echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}  Setup complete!${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""
echo "  To run the simulator:"
echo "    cd $SIM_DIR/build"
echo "    ./bin/main"
echo ""
echo "  To run with live-updating mock data:"
echo "    python3 $PROJECT_DIR/scripts/mock_data.py &"
echo "    cd $SIM_DIR/build && ./bin/main"
echo ""
echo "  The display window will open at 320×240."
echo "  Press Ctrl+C to quit."
echo ""
