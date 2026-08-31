@echo off
chcp 65001 >nul
REM ============================================================
REM  F4HWN Fusion firmware build script (Windows)
REM
REM  Usage:
REM    compile-firmware.bat               build Fusion + Doppler (default)
REM    compile-firmware.bat --plain       build Fusion without Doppler
REM    compile-firmware.bat Custom        build another CMake preset
REM    compile-firmware.bat --clean       remove build dir first (full rebuild)
REM
REM  Output: build\<preset>\f4hwn.*.bin
REM ============================================================

setlocal enabledelayedexpansion

set "PRESET=Fusion"
set "DOPPLER=ON"
set "CLEAN=0"

:parse_args
if "%1"=="" goto args_done
if /i "%1"=="--plain"  set "DOPPLER=OFF"
if /i "%1"=="--clean"  set "CLEAN=1"
if /i "%1"=="--help"   goto usage
if not "%1"=="--plain" if not "%1"=="--clean" if not "%1"=="--help" set "PRESET=%1"
shift
goto parse_args
:args_done

REM ---- toolchain paths (edit here if paths change) ----
set "TOOLCHAIN=C:\Users\17507\toolchain\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin"
set "NINJA=D:\VS2026\VS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
set "CMAKE=D:\CMake\bin"
set "PATH=%TOOLCHAIN%;%NINJA%;%CMAKE%;%PATH%"

REM ---- sanity checks ----
if not exist "%TOOLCHAIN%\arm-none-eabi-gcc.exe" (
    echo [ERROR] ARM toolchain not found: %TOOLCHAIN%
    goto fail
)
if not exist "%NINJA%\ninja.exe" (
    echo [ERROR] ninja not found: %NINJA%
    goto fail
)
if not exist "%CMAKE%\cmake.exe" (
    echo [ERROR] cmake not found: %CMAKE%
    goto fail
)

cd /d "%~dp0"

echo ============================================================
echo  Preset : %PRESET%
echo  Doppler: %DOPPLER%
echo  Clean  : %CLEAN%
echo ============================================================

if "%CLEAN%"=="1" (
    echo Removing build\%PRESET% ...
    if exist "build\%PRESET%" rmdir /s /q "build\%PRESET%"
)

if "%DOPPLER%"=="ON" (
    cmake --preset %PRESET% -DENABLE_FEAT_F4HWN_DOPPLER=ON
) else (
    cmake --preset %PRESET%
)
if errorlevel 1 (
    echo [ERROR] CMake configure failed
    goto fail
)

cmake --build build\%PRESET%
if errorlevel 1 (
    echo [ERROR] Build failed
    goto fail
)

REM Ensure .bin is generated (CMake POST_BUILD may silently skip objcopy)
if not exist "build\%PRESET%\f4hwn.%PRESET%.bin" (
    echo [INFO] Generating f4hwn.%PRESET%.bin ...
    "%TOOLCHAIN%\arm-none-eabi-objcopy.exe" -O binary "build\%PRESET%\f4hwn.%PRESET%.elf" "build\%PRESET%\f4hwn.%PRESET%.bin"
    if errorlevel 1 (
        echo [ERROR] objcopy failed
        goto fail
    )
)

echo.
echo ============================================================
echo  Build OK. Artifacts:
for %%f in (build\%PRESET%\f4hwn.*.bin) do (
    echo    %%~ff  ^(%%~zf bytes^)
)
echo  Flash firmware: build\%PRESET%\f4hwn.%PRESET%.bin
echo  (UV Studio - Flash Firmware - select the .bin file)
echo ============================================================
goto done

:usage
echo Usage:
echo   compile-firmware.bat [preset] [--plain] [--clean] [--help]
echo     preset   CMake preset name (default: Fusion)
echo     --plain  build without Doppler feature
echo     --clean  full rebuild (delete build dir first)
goto done

:fail
echo.
echo Build FAILED.
exit /b 1

:done
endlocal
