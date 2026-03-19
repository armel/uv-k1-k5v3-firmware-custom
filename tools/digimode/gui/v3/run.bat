@echo off
setlocal

echo ================================================
echo   UV-K1/K5V3 Digmode Control v3 - Run
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
    if %errorlevel% neq 0 (
        echo [ERROR] Failed to create virtual environment.
        pause
        exit /b 1
    )
) else (
    echo [1/4] Virtual environment already exists.
)

call .venv\Scripts\activate.bat
if %errorlevel% neq 0 (
    echo [ERROR] Failed to activate virtual environment.
    pause
    exit /b 1
)

echo [2/4] Installing dependencies ...
pip install -r requirements.txt -q
if %errorlevel% neq 0 (
    echo [ERROR] Failed to install dependencies.
    pause
    exit /b 1
)

echo [3/4] Starting GUI ...
python digmode_guiv3.py
set EXIT_CODE=%errorlevel%

echo.
echo ================================================
echo [4/4] GUI exited with code %EXIT_CODE%
echo ================================================
echo.
pause
exit /b %EXIT_CODE%
