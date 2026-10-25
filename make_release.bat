@echo off
rem ============================================================
rem  Splatterhouse Recompiled - Package release
rem  Creates "splatterhouse-decomp-release\" with everything the end
rem  user needs (drag the ISO onto install.bat).
rem ============================================================
setlocal enableextensions
title Splatterhouse Recompiled - Create release

set "ROOT=%~dp0"
set "OUT=%ROOT%splatterhouse-decomp-release"
set "BUILD=%ROOT%out\build\win-amd64-relwithdebinfo"
set "SDK=%ROOT%extern\rexglue-sdk\out\win-amd64"

echo ===============================================
echo   Splatterhouse Recompiled - Create release
echo ===============================================
echo   Build source : %BUILD%
echo   Target       : %OUT%
echo.

if not exist "%BUILD%\splatterhouse.exe" goto no_build
if not exist "%ROOT%install.bat" goto no_install

rem Clean the previous release.
if exist "%OUT%" rmdir /S /Q "%OUT%"
mkdir "%OUT%"
mkdir "%OUT%\tools"
mkdir "%OUT%\docs"
mkdir "%OUT%\assets"

echo [1/7] Copying executable...
copy /Y "%BUILD%\splatterhouse.exe" "%OUT%\splatterhouse.exe" >nul
if errorlevel 1 goto failed

echo [2/7] Copying runtime DLLs...
copy /Y "%BUILD%\rexruntimerd.dll"  "%OUT%\" >nul
if errorlevel 1 goto failed
copy /Y "%BUILD%\rexgpu-xenosrd.dll" "%OUT%\" >nul
if errorlevel 1 goto failed
copy /Y "%BUILD%\TracyClientrd.dll"  "%OUT%\" >nul
if errorlevel 1 goto failed
rem FidelityFX (required if the runtime was built with REXGLUE_ENABLE_FIDELITYFX=ON).
if exist "%BUILD%\amd_fidelityfx_dx12drel.dll" copy /Y "%BUILD%\amd_fidelityfx_dx12drel.dll" "%OUT%\" >nul

echo [3/7] Copying installer and tools...
copy /Y "%ROOT%install.bat" "%OUT%\install.bat" >nul
copy /Y "%ROOT%tools\extract-xiso.exe" "%OUT%\tools\" >nul
if errorlevel 1 goto failed
copy /Y "%ROOT%tools\xextool.exe" "%OUT%\tools\" >nul
if errorlevel 1 goto failed

echo [4/7] Default configuration...
rem The build's splatterhouse.toml is NOT copied: it may be the development
rem config (vulkan backend, log_level=info...). install.bat creates it on the
rem user's machine, and also picks the internal resolution based on the GPU.
if exist "%OUT%\splatterhouse.toml" del "%OUT%\splatterhouse.toml"
echo        generated during installation (install.bat)

echo [5/7] Copying precompiled shader cache...
set "SHCACHE_SRC="
if exist "%ROOT%shadercache" set "SHCACHE_SRC=%ROOT%shadercache"
for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "[Environment]::GetFolderPath('MyDocuments')"`) do set "DOCS=%%D"
if not defined SHCACHE_SRC if exist "%DOCS%\splatterhouse\cache\shaders\shareable" set "SHCACHE_SRC=%DOCS%\splatterhouse\cache\shaders\shareable"
if not defined SHCACHE_SRC goto skip_shaders
mkdir "%OUT%\shadercache" 2>nul
xcopy /Y /I "%SHCACHE_SRC%\*" "%OUT%\shadercache\" >nul
echo        from: %SHCACHE_SRC%
:skip_shaders

echo [6/7] Copying documentation...
if exist "%ROOT%README.md" copy /Y "%ROOT%README.md" "%OUT%\README.md" >nul
if exist "%ROOT%docs" xcopy /E /I /Y "%ROOT%docs\*" "%OUT%\docs\" >nul

echo [7/7] Creating README.txt...
> "%OUT%\README.txt" echo Splatterhouse Recompiled - Native port (Xbox 360 to PC)
>>"%OUT%\README.txt" echo =====================================================
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo USAGE:
>>"%OUT%\README.txt" echo   1. Drag the Splatterhouse ISO onto install.bat
>>"%OUT%\README.txt" echo   2. Wait for it to extract the game to "assets\"
>>"%OUT%\README.txt" echo   3. Run splatterhouse.exe
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo IN-GAME OPTIONS:
>>"%OUT%\README.txt" echo   F5  - Options menu (graphics, language, unlock levels)
>>"%OUT%\README.txt" echo   F3  - Debug overlay (FPS)
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo DLC:
>>"%OUT%\README.txt" echo   Create a "dlc" folder next to splatterhouse.exe and put
>>"%OUT%\README.txt" echo   the DLC packages (CON/PIRS/LIVE) there. The port installs them automatically.
>>"%OUT%\README.txt" echo   This also works: %%USERPROFILE%%\Documents\splatterhouse\4E4D07F0\00000002\
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo ULTRAWIDE:
>>"%OUT%\README.txt" echo   F5 -^> Internal resolution -^> 2560x1080 or 3440x1440 (the game
>>"%OUT%\README.txt" echo   renders native 21:9; no bars or stretching).
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo IF THE GAME CLOSES ON STARTUP:
>>"%OUT%\README.txt" echo   The game contents are missing. See assets_error.txt next to the
>>"%OUT%\README.txt" echo   executable (it says which folder was checked and what is missing).
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo LEGAL NOTICE: you need your own legitimate copy of the game.
>>"%OUT%\README.txt" echo No game data is included in this package.
>>"%OUT%\README.txt" echo.

echo.
echo ===============================================
echo   Release created in:
echo   %OUT%
echo ===============================================
echo.
dir /B "%OUT%"
goto done

:no_build
echo ERROR: "%BUILD%\splatterhouse.exe" does not exist.
echo Build first (preset win-amd64-relwithdebinfo):
echo   cmake --preset win-amd64-relwithdebinfo
echo   cmake --build out\build\win-amd64-relwithdebinfo --target splatterhouse -j 6
goto done

:no_install
echo ERROR: cannot find install.bat in the root.
goto done

:failed
echo ERROR: failed to copy files.

:done
echo.
pause
