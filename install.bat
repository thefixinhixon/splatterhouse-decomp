@echo off
setlocal enableextensions
title Splatterhouse Recompiled - Installer

set "ROOT=%~dp0"
set "TOOL=%ROOT%tools\extract-xiso.exe"
set "XEXTOOL=%ROOT%tools\xextool.exe"
set "ASSETS=%ROOT%assets"

echo ===============================================
echo   Splatterhouse Recompiled - Installer
echo ===============================================
echo.

set "ISO=%~1"
if not "%ISO%"=="" goto have_iso

echo Drag the Splatterhouse ISO here, or paste its path, and press Enter:
set /p ISO=
if "%ISO%"=="" goto no_iso

:have_iso
set "ISO=%ISO:"=%"
if not exist "%ISO%" goto no_iso_file
if not exist "%TOOL%" goto no_tool

rem Default config (without it the GPU isn't loaded -> black screen)
if exist "%ROOT%splatterhouse.toml" goto have_toml
echo Creating default splatterhouse.toml...

rem Internal resolution per GPU. On the ROV (interlock) path the cost scales
rem with pixels and on AMD/Intel it is much more expensive than on NVIDIA
rem (issue #3): in combat with several enemies 1080p can drop to a few FPS. The
rem game is from 2010 and renders at 720p, so FSR scales it back up to the
rem window resolution.
set "VIDEO_W=1920"
set "VIDEO_H=1080"
set "GPU="
for /f "usebackq delims=" %%G in (`powershell -NoProfile -Command "(Get-CimInstance Win32_VideoController | Where-Object { $_.AdapterRAM -gt 0 -and $_.Name -notmatch 'Basic|Virtual|Remote|Parsec|Sunshine|Meta' } | Sort-Object AdapterRAM -Descending | Select-Object -First 1 -ExpandProperty Name)" 2^>nul`) do set "GPU=%%G"
if not defined GPU goto have_gpu
echo   Detected GPU: %GPU%
echo %GPU% > "%TEMP%\sh_gpu.txt"
findstr /I /C:"AMD" /C:"Radeon" /C:"Intel" /C:"Arc" "%TEMP%\sh_gpu.txt" >nul
if errorlevel 1 goto have_gpu
set "VIDEO_W=1280"
set "VIDEO_H=720"
echo   AMD/Intel: internal resolution 1280x720 + FSR (can be raised in F5)
:have_gpu
echo # Splatterhouse Recompiled - configuration> "%ROOT%splatterhouse.toml"
echo log_level = "warn">> "%ROOT%splatterhouse.toml"
echo log_file = "splatterhouse.log">> "%ROOT%splatterhouse.toml"
echo gpu_plugin = "xenos">> "%ROOT%splatterhouse.toml"
echo render_target_path_d3d12 = "rov">> "%ROOT%splatterhouse.toml"
echo present_effect = "fsr">> "%ROOT%splatterhouse.toml"
echo video_mode_width = %VIDEO_W%>> "%ROOT%splatterhouse.toml"
echo video_mode_height = %VIDEO_H%>> "%ROOT%splatterhouse.toml"

echo fullscreen = true>> "%ROOT%splatterhouse.toml"
echo vsync = false>> "%ROOT%splatterhouse.toml"
echo d3d12_present_frame_limiter = true>> "%ROOT%splatterhouse.toml"
echo d3d12_present_frame_limiter_fps = 60>> "%ROOT%splatterhouse.toml"
echo d3d12_allow_variable_refresh_rate_and_tearing = false>> "%ROOT%splatterhouse.toml"
echo d3d12_present_adaptive_tearing = true>> "%ROOT%splatterhouse.toml"
echo video_mode_refresh_rate = 60>> "%ROOT%splatterhouse.toml"
echo bind_graphics = "F5">> "%ROOT%splatterhouse.toml"
echo sh_language = "auto">> "%ROOT%splatterhouse.toml"
:have_toml

echo.
echo Extracting the game...
echo   ISO    : %ISO%
echo   Target : %ASSETS%
echo.

set "TMP=%TEMP%\splatterhouse_install"
if exist "%TMP%" rmdir /S /Q "%TMP%"
mkdir "%TMP%"

pushd "%TMP%"
"%TOOL%" -x -s "%ISO%"
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" goto failed

rem The contents end up in %TMP%\<iso name> (or one level deeper if the ISO
rem contains an extra folder).
set "SRC="
for /d %%D in ("%TMP%\*") do if exist "%%D\default.xex" set "SRC=%%D"
if not defined SRC for /d %%D in ("%TMP%\*") do for /d %%E in ("%%D\*") do if exist "%%E\default.xex" set "SRC=%%E"
if not defined SRC goto failed

if exist "%ASSETS%" goto copy_into
move "%SRC%" "%ASSETS%" >nul
goto ok

:copy_into
xcopy /E /I /Y "%SRC%\*" "%ASSETS%" >nul
rmdir /S /Q "%SRC%"

:ok
rem Check: without the data folder the game starts to a black screen and ends
rem with an access violation (issue #4). Better to warn here.
if not exist "%ASSETS%\default.xex" goto bad_extract
if not exist "%ASSETS%\data" goto bad_extract
if not exist "%XEXTOOL%" goto skip_xex
if not exist "%ASSETS%\default.xex" goto skip_xex
echo.
echo Decrypting the XEX to default.dec.xex...
"%XEXTOOL%" -e u -c b -o "%ASSETS%\default.dec.xex" "%ASSETS%\default.xex"
if errorlevel 1 echo   Warning: xextool failed; the original default.xex will be used.
:skip_xex

rem Precompiled shader cache included in the release (avoids stutter when
rem compiling shaders the first time; see docs/rov-performance.md).
if not exist "%ROOT%shadercache" goto skip_shaders
for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "[Environment]::GetFolderPath('MyDocuments')"`) do set "DOCS=%%D"
if "%DOCS%"=="" goto skip_shaders
set "SHCACHE=%DOCS%\splatterhouse\cache\shaders\shareable"
if not exist "%SHCACHE%" mkdir "%SHCACHE%"
echo Copying precompiled shader cache...
xcopy /Y /I "%ROOT%shadercache\*" "%SHCACHE%" >nul
:skip_shaders

rmdir /S /Q "%TMP%"
echo.
echo Done. Run splatterhouse.exe to play.
goto done

:no_iso
echo No ISO was given.
goto done
:no_iso_file
echo Cannot find the ISO: %ISO%
goto done
:no_tool
echo Cannot find tools\extract-xiso.exe
goto done
:failed
echo.
echo The extraction did not complete.
echo Check that the ISO is Splatterhouse and that the path has no unusual characters.
goto done

:bad_extract
echo.
echo The extraction did not produce the expected layout (default.xex + data folder).
echo Check that the ISO is the Xbox 360 Splatterhouse and try again.
rmdir /S /Q "%TMP%" 2>nul

:done
echo.
pause
