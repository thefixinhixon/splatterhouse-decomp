#Requires -Version 5.1
# Windows environment setup for Splatterhouse Recompiled
param(
    [switch]$InstallTools
)

$ErrorActionPreference = "Stop"
Write-Host "[setup] Splatterhouse Recompiled - Setup Windows" -ForegroundColor Cyan

function Test-Cmd($name) { $null -ne (Get-Command $name -ErrorAction SilentlyContinue) }

# 1. Git
if (-not (Test-Cmd git)) {
    Write-Host "[setup] Git not found." -ForegroundColor Yellow
    if ($InstallTools) {
        winget install --id Git.Git -e --accept-package-agreements --accept-source-agreements
    } else {
        Write-Host "  Install Git: winget install Git.Git  (or https://git-scm.com/download/win)" -ForegroundColor Yellow
    }
} else { Write-Host "[ok] git $(git --version)" -ForegroundColor Green }

# 2. CMake
if (-not (Test-Cmd cmake)) {
    Write-Host "[setup] CMake not found." -ForegroundColor Yellow
    if ($InstallTools) { winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements }
    else { Write-Host "  winget install Kitware.CMake" -ForegroundColor Yellow }
} else { Write-Host "[ok] cmake $(cmake --version | Select-Object -First 1)" -ForegroundColor Green }

# 3. Ninja
if (-not (Test-Cmd ninja)) {
    Write-Host "[setup] Ninja not found." -ForegroundColor Yellow
    if ($InstallTools) { winget install --id Ninja-build.Ninja -e --accept-package-agreements --accept-source-agreements }
    else { Write-Host "  winget install Ninja-build.Ninja" -ForegroundColor Yellow }
} else { Write-Host "[ok] ninja $(ninja --version)" -ForegroundColor Green }

# 4. Clang (ReXGlue requires Clang 18+, 20.x recommended)
if (Test-Cmd clang) {
    Write-Host "[ok] clang $(clang --version | Select-Object -First 1)" -ForegroundColor Green
    $clangVer = (clang --version)[0]
    if ($clangVer -match "version (\d+)\." -and [int]$Matches[1] -lt 18) {
        Write-Host "[warn] ReXGlue requires Clang 18+ (found: $clangVer)." -ForegroundColor Yellow
        Write-Host "  Install LLVM with: winget install LLVM.LLVM" -ForegroundColor Yellow
    }
} else {
    Write-Host "[warn] Clang not found. ReXGlue requires it (winget install LLVM.LLVM)." -ForegroundColor Yellow
    Write-Host "  On Windows the Visual Studio 2022 'C++ Clang Compiler for Windows' also works." -ForegroundColor Yellow
}

# 5. Submodules (rexglue-sdk; nested submodules are fetched with --recursive)
if (Test-Path ".git") {
    Write-Host "[setup] Initializing rexglue-sdk submodule..." -ForegroundColor Cyan
    git submodule update --init extern/rexglue-sdk
    Write-Host "[ok] rexglue-sdk ready" -ForegroundColor Green
} else {
    Write-Host "[info] Not a git repo yet. Initialize with: git init; git submodule..." -ForegroundColor Yellow
    Write-Host "  Or clone with --recursive if it is already a remote repo." -ForegroundColor Yellow
}

# 6. Build bootstrap
Write-Host "`n[setup] To build (without XEX):" -ForegroundColor Cyan
Write-Host "  cmake --preset win-amd64-release"
Write-Host "  cmake --build out/build/win-amd64-release -j"
Write-Host "`n[setup] For codegen with the XEX (requires the rexglue CLI):" -ForegroundColor Cyan
Write-Host "  python tools/recompile.py --xex game/default.dec.xex"
Write-Host "  cmake --build out/build/win-amd64-release -j"

Write-Host "`n[setup] Done." -ForegroundColor Green
