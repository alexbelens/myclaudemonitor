# ============================================================
# Claude Monitor CYD - Windows Setup (PowerShell)
# ============================================================
# Run this in PowerShell as Administrator:
#   Set-ExecutionPolicy Bypass -Scope Process
#   .\setup_windows.ps1
# ============================================================

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Claude Monitor CYD - Windows Setup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$PROJECT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$SIM_DIR = Join-Path $PROJECT_DIR "lv_port_pc_vscode"

# ----------------------------------------------------------
# 1. Check / install prerequisites
# ----------------------------------------------------------
Write-Host "[1/6] Checking prerequisites..." -ForegroundColor Yellow

# Check Git
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "  Git not found. Installing via winget..." -ForegroundColor Gray
    winget install --id Git.Git -e --accept-package-agreements --accept-source-agreements
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
}
Write-Host "  Git: OK" -ForegroundColor Green

# Check CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "  CMake not found. Installing via winget..." -ForegroundColor Gray
    winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
}
Write-Host "  CMake: OK" -ForegroundColor Green

# Check Python
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Host "  Python not found. Installing via winget..." -ForegroundColor Gray
    winget install --id Python.Python.3.12 -e --accept-package-agreements --accept-source-agreements
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
}
Write-Host "  Python: OK" -ForegroundColor Green

# ----------------------------------------------------------
# 2. Install vcpkg + SDL2
# ----------------------------------------------------------
Write-Host ""
Write-Host "[2/6] Installing SDL2 via vcpkg..." -ForegroundColor Yellow

$VCPKG_DIR = Join-Path $PROJECT_DIR "vcpkg"

if (-not (Test-Path $VCPKG_DIR)) {
    git clone https://github.com/microsoft/vcpkg.git $VCPKG_DIR
    & "$VCPKG_DIR\bootstrap-vcpkg.bat" -disableMetrics
}

$VCPKG_EXE = Join-Path $VCPKG_DIR "vcpkg.exe"

# Install SDL2 for the correct triplet
& $VCPKG_EXE install sdl2:x64-windows
Write-Host "  SDL2: OK" -ForegroundColor Green

# Set CMAKE_TOOLCHAIN for vcpkg
$VCPKG_CMAKE = Join-Path $VCPKG_DIR "scripts\buildsystems\vcpkg.cmake"

# ----------------------------------------------------------
# 3. Clone LVGL PC simulator
# ----------------------------------------------------------
Write-Host ""
Write-Host "[3/6] Cloning LVGL PC simulator..." -ForegroundColor Yellow

if (Test-Path $SIM_DIR) {
    Write-Host "  Already cloned. Updating..." -ForegroundColor Gray
    Push-Location $SIM_DIR
    git pull
    git submodule update --init --recursive
    Pop-Location
} else {
    git clone --recursive https://github.com/lvgl/lv_port_pc_vscode $SIM_DIR
}
Write-Host "  LVGL simulator: OK" -ForegroundColor Green

# ----------------------------------------------------------
# 4. Copy Claude Monitor source files
# ----------------------------------------------------------
Write-Host ""
Write-Host "[4/6] Installing Claude Monitor UI..." -ForegroundColor Yellow

# Find the main source directory
$MAIN_SRC_DIR = $null
@("main\src", "main") | ForEach-Object {
    $candidate = Join-Path $SIM_DIR $_
    if (Test-Path $candidate) { $MAIN_SRC_DIR = $candidate }
}

if (-not $MAIN_SRC_DIR) {
    # Fallback: just use main/src
    $MAIN_SRC_DIR = Join-Path $SIM_DIR "main\src"
    New-Item -ItemType Directory -Path $MAIN_SRC_DIR -Force | Out-Null
}

# Backup original main.c
$ORIGINAL_MAIN = Join-Path $MAIN_SRC_DIR "main.c"
if (Test-Path $ORIGINAL_MAIN) {
    Copy-Item $ORIGINAL_MAIN "$ORIGINAL_MAIN.original" -Force
}

# Copy our files
Copy-Item (Join-Path $PROJECT_DIR "src\main.c") $MAIN_SRC_DIR -Force
Copy-Item (Join-Path $PROJECT_DIR "src\claude_monitor_ui.h") $MAIN_SRC_DIR -Force

Write-Host "  UI files copied to: $MAIN_SRC_DIR" -ForegroundColor Green

# ----------------------------------------------------------
# 5. Patch CMakeLists for 320x240
# ----------------------------------------------------------
Write-Host ""
Write-Host "[5/6] Patching for CYD resolution (320x240)..." -ForegroundColor Yellow

$CMAKE_FILE = Join-Path $SIM_DIR "CMakeLists.txt"
$cmakeContent = Get-Content $CMAKE_FILE -Raw
if ($cmakeContent -notmatch "SDL_HOR_RES") {
    $patchLine = "`n# Claude Monitor: CYD resolution`nadd_compile_definitions(SDL_HOR_RES=320 SDL_VER_RES=240)"
    Add-Content $CMAKE_FILE $patchLine
}
Write-Host "  CMakeLists.txt patched." -ForegroundColor Green

# ----------------------------------------------------------
# 6. Build
# ----------------------------------------------------------
Write-Host ""
Write-Host "[6/6] Building..." -ForegroundColor Yellow

Push-Location $SIM_DIR
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_CMAKE"
cmake --build build --config Release
Pop-Location

# ----------------------------------------------------------
# Generate initial mock data
# ----------------------------------------------------------
Write-Host ""
Write-Host "Generating mock data..." -ForegroundColor Yellow
python (Join-Path $PROJECT_DIR "scripts\mock_data.py") --once

# ----------------------------------------------------------
# Done!
# ----------------------------------------------------------
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Setup complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "  To run the mock data generator:" -ForegroundColor White
Write-Host "    python scripts\mock_data.py --demo" -ForegroundColor Gray
Write-Host ""
Write-Host "  To run the simulator:" -ForegroundColor White

# Find the built executable
$exePaths = @(
    (Join-Path $SIM_DIR "build\bin\Release\main.exe"),
    (Join-Path $SIM_DIR "build\bin\main.exe"),
    (Join-Path $SIM_DIR "build\Release\main.exe"),
    (Join-Path $SIM_DIR "build\main.exe")
)
$foundExe = $exePaths | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($foundExe) {
    Write-Host "    $foundExe" -ForegroundColor Gray
} else {
    Write-Host "    cd lv_port_pc_vscode\build and look for main.exe" -ForegroundColor Gray
}

Write-Host ""
Write-Host "  The 320x240 window will show the Claude Monitor dashboard."
Write-Host "  Close the window or press Ctrl+C to quit."
Write-Host ""
