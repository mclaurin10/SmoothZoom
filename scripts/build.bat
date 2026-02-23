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
cmake --build . --config %CONFIG% --parallel

echo.
echo Build complete: %BUILD_DIR%\%CONFIG%\
echo Remember to sign before running: signtool sign /n "SmoothZoom Dev" /fd SHA256 %CONFIG%\SmoothZoom.exe
