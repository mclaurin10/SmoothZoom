@echo off
REM =============================================================================
REM SmoothZoom — Windows Build Script
REM Run from a "x64 Native Tools Command Prompt for VS 2022"
REM =============================================================================

set BUILD_DIR=build
set CONFIG=Debug

if "%1"=="release" set CONFIG=Release

if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

cmake -G "Visual Studio 17 2022" -A x64 ..
if errorlevel 1 (
    echo CMake configure FAILED.
    exit /b 1
)
cmake --build . --config %CONFIG% --parallel
if errorlevel 1 (
    echo CMake build FAILED.
    exit /b 1
)

echo.
echo Build complete: %BUILD_DIR%\%CONFIG%\
echo Sign + install ^(elevated^): scripts\deploy-machinestore.ps1 -Config %CONFIG%
echo   ^(or: scripts\sign-binary.ps1 then scripts\install-secure.ps1^)
