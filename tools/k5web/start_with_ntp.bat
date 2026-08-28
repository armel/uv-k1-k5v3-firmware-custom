@echo off
chcp 65001 >nul
title K5Web NTP Time Proxy Launcher
setlocal

cd /d "%~dp0"

set "PROXY_URL=http://127.0.0.1:8765/time"
set "HTTP_PORT=8080"

REM Find Python
set "PYTHON="
for %%P in (python python3 py) do (
    if "%PYTHON%"=="" (
        where %%P > nul 2> nul
        if not errorlevel 1 set "PYTHON=%%P"
    )
)
if "%PYTHON%"=="" (
    echo [ERROR] Python not found. Please install Python 3 first.
    pause
    exit /b 1
)
echo Python: %PYTHON%

REM Start NTP proxy if not running
curl -s %PROXY_URL% > nul 2> nul
if not errorlevel 1 (
    echo [INFO] NTP proxy already running at %PROXY_URL%
    goto ntp_done
)

echo Starting NTP proxy...
start /min "K5Web NTP Proxy" %PYTHON% time_proxy.py

echo Waiting for NTP proxy...
set /a RETRIES=30
:wait_proxy
curl -s %PROXY_URL% > nul 2> nul
if not errorlevel 1 goto proxy_ready
set /a RETRIES-=1
if %RETRIES% leq 0 (
    echo [WARN] NTP proxy start timeout. Run manually: python time_proxy.py
    goto ntp_done
)
timeout /t 1 /nobreak > nul 2> nul
goto wait_proxy
:proxy_ready
echo [INFO] NTP proxy ready at %PROXY_URL%
:ntp_done

REM Start HTTP server if not running
set "PAGE_URL=http://127.0.0.1:%HTTP_PORT%/index.html"
curl -s http://127.0.0.1:%HTTP_PORT%/ > nul 2> nul
if not errorlevel 1 (
    echo [INFO] HTTP server already running
    goto http_done
)

echo Starting local HTTP server...
start /min "K5Web HTTP Server" %PYTHON% -m http.server %HTTP_PORT%

echo Waiting for HTTP server...
set /a RETRIES=30
:wait_http
curl -s http://127.0.0.1:%HTTP_PORT%/ > nul 2> nul
if not errorlevel 1 goto http_ready
set /a RETRIES-=1
if %RETRIES% leq 0 (
    echo [WARN] HTTP server start timeout, using file://
    set "PAGE_URL=file://%~dp0index.html"
    goto http_done
)
timeout /t 1 /nobreak > nul 2> nul
goto wait_http
:http_ready
echo [INFO] HTTP server ready at http://127.0.0.1:%HTTP_PORT%/
:http_done

echo Opening page: %PAGE_URL%
start "" %PAGE_URL%

echo.
echo ============================================================
echo  NTP Proxy: %PROXY_URL%
echo  Web Tool:  %PAGE_URL%
echo ============================================================
echo.
echo Notes:
echo   1. You can close this window; proxy/server keep running.
echo   2. To stop them, end Python processes in Task Manager.
echo.
pause
