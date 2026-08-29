@echo off
cd /d "%~dp0"

REM Build k5web_server.exe statically with MSVC (/MT runtime).
REM Locates Visual Studio via vswhere and initializes the x64 toolchain,
REM so this script works from any plain cmd/PowerShell window.
REM NOTE: vcvars64.bat must run under the default codepage - do NOT chcp 65001 before it.

set "VSDIR="
for /f "usebackq delims=" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo [ERROR] Visual Studio C++ toolchain not found.
    echo Install the "Desktop development with C++" workload, then re-run.
    pause
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [ERROR] Failed to initialize the MSVC x64 environment.
    pause
    exit /b 1
)

chcp 65001 >nul
cl /O2 /MT /W4 /Fe:k5web_server.exe k5web_server.cpp
if errorlevel 1 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo [OK] k5web_server.exe built (static, no Python runtime needed).
pause
