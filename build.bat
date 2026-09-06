@echo off
setlocal enabledelayedexpansion

rem UnityRuntimeExplorer build launcher.
rem   build.bat                     -> interactive menu
rem   build.bat msvc release        -> non-interactive
rem   build.bat clang debug clean   -> non-interactive + wipe build dir
rem   build.bat clang release tests -> non-interactive + build contract tests

set "COMPILER=%~1"
set "CONFIG=%~2"
set "EXTRA=%~3"

if "%COMPILER%"=="" (
    echo === UnityRuntimeExplorer Build ===
    echo.
    echo Compiler:
    echo   [1] Clang
    echo   [2] MSVC  ^(cl.exe^)
    choice /c 12 /n /m "Select compiler: "
    if errorlevel 2 (set "COMPILER=msvc") else (set "COMPILER=clang")
    echo.
)

if "%CONFIG%"=="" (
    echo Configuration:
    echo   [1] Release
    echo   [2] Debug
    choice /c 12 /n /m "Select configuration: "
    if errorlevel 2 (set "CONFIG=debug") else (set "CONFIG=release")
    echo.
)

if "%EXTRA%"=="" (
    choice /c yn /n /m "Clean the existing build output first? (y/n): "
    if errorlevel 2 (set "EXTRA=") else (set "EXTRA=clean")
    echo.
)

set "PS_ARGS=-Compiler %COMPILER% -Config %CONFIG%"
if /i "%EXTRA%"=="clean" set "PS_ARGS=%PS_ARGS% -Clean"
if /i "%EXTRA%"=="tests" set "PS_ARGS=%PS_ARGS% -Tests"

echo Running: powershell -File build.ps1 %PS_ARGS%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %PS_ARGS%
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (
    echo Build finished successfully.
) else (
    echo Build failed with exit code %RC%.
)

exit /b %RC%
