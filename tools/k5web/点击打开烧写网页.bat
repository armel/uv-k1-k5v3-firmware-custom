@echo off
chcp 65001 >nul
title K5Web Server Launcher
setlocal

cd /d "%~dp0"

set "HTTP_PORT=8080"
set "NTP_PORT=8765"
set "PAGE_URL=http://127.0.0.1:%HTTP_PORT%/index.html"

if not exist "k5web_server.exe" (
    echo [ERROR] k5web_server.exe not found.
    echo.
    echo Please compile the C++ server first:
    echo   - MinGW: double-click compile_k5web_server.bat
    echo   - MSVC:  double-click compile_k5web_server_msvc.bat
    echo.
    echo To use the Python version instead, run: python time_proxy.py
    pause
    exit /b 1
)

REM Check if server is already running
curl -s http://127.0.0.1:%HTTP_PORT%/ > nul 2> nul
if not errorlevel 1 (
    echo [INFO] K5Web server already running
    goto open_page
)

echo Starting K5Web server (NTP proxy + HTTP file server)...
start /min "K5Web Server" k5web_server.exe

echo Waiting for server...
set /a RETRIES=30
:wait_loop
curl -s http://127.0.0.1:%HTTP_PORT%/ > nul 2> nul
if not errorlevel 1 goto ready
set /a RETRIES-=1
if %RETRIES% leq 0 (
    echo [WARN] Server start timeout, falling back to file://
    set "PAGE_URL=file://%~dp0index.html"
    goto open_page
)
timeout /t 1 /nobreak > nul 2> nul
goto wait_loop

:ready
echo [INFO] Server ready

:open_page
echo Opening page: %PAGE_URL%
start "" %PAGE_URL%

echo.
echo ============================================================
echo  NTP proxy: http://127.0.0.1:%NTP_PORT%/time
echo  Web tool: %PAGE_URL%
echo ============================================================
echo.
echo Notes:
echo   1. You can close this window; the server keeps running.
echo   2. To stop it, end k5web_server.exe in Task Manager.
echo.
pause
