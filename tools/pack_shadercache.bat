@echo off
rem Packages the SDK shader cache (generated while playing) into the release's
rem "shadercache\", to distribute it with install.bat.
rem Usage: play once (go through levels/menus/cutscenes) and then run this script.
setlocal enableextensions
title Splatterhouse Recompiled - Package shader cache

set "ROOT=%~dp0.."
for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "[Environment]::GetFolderPath('MyDocuments')"`) do set "DOCS=%%D"
set "SRC=%DOCS%\splatterhouse\cache\shaders\shareable"
set "DST=%ROOT%\shadercache"

echo ===============================================
echo   Package shader cache
echo ===============================================
echo.
echo   Source : %SRC%
echo   Target : %DST%
echo.

if not exist "%SRC%" goto no_src
if not exist "%DST%" mkdir "%DST%"

echo Copying...
xcopy /Y /I "%SRC%\*" "%DST%" >nul
if errorlevel 1 goto failed

echo.
echo Done. The "shadercache" folder can now be shipped in the release.
goto done

:no_src
echo The shader cache does not exist. Play once before packaging.
goto done

:failed
echo Error copying the cache.

:done
echo.
pause
