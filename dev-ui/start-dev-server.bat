@echo off
echo ============================================================
echo ESP32 Boat Monitor - Development UI Setup
echo ============================================================
echo.

REM Check if node_modules exists
if not exist "node_modules\" (
    echo [1/2] Installing dependencies...
    echo.
    call npm install
    echo.
) else (
    echo [SKIP] Dependencies already installed
    echo.
)

echo [2/2] Starting mock server...
echo.
echo Press Ctrl+C to stop the server when done
echo.
call npm start
