@echo off
chcp 65001 >nul
cd /d "%~dp0"

REM Build k5web_server.exe statically with MinGW-w64.
REM Install MinGW-w64 and make sure g++ is in PATH.

set "CXX=g++"
where %CXX% > nul 2> nul
if errorlevel 1 (
    echo [ERROR] g++ not found. Please install MinGW-w64 and add its bin directory to PATH.
    pause
    exit /b 1
)

%CXX% -O2 -s -static -static-libgcc -static-libstdc++ -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS k5web_server.cpp -o k5web_server.exe -lws2_32 -lwininet
if errorlevel 1 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo [OK] k5web_server.exe built (static, no Python runtime needed).
pause
