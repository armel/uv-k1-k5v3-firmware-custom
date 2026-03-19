@echo off
setlocal

echo ================================================
echo   UV-K1/K5V3 Digmode Control v3 - Build
echo ================================================
echo.

python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] python not found in PATH.
    pause
    exit /b 1
)

if not exist .venv (
    echo [1/4] Creating virtual environment ...
    python -m venv .venv
) else (
    echo [1/4] Virtual environment already exists.
)

call .venv\Scripts\activate.bat

echo [2/4] Installing dependencies ...
pip install -r requirements.txt -q
if %errorlevel% neq 0 (
    echo [ERROR] Failed to install dependencies.
    pause
    exit /b 1
)

echo [3/4] Building executable ...
pyinstaller --clean DigmodeControlV3.spec
if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo.
echo ================================================
echo [4/4] Done!
echo   Executable: dist\DigmodeControlV3.exe
echo ================================================
echo.
pause
