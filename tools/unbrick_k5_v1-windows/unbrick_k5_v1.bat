@echo off
REM --- Script for Windows OpenOCD.exe ---
openocd.exe -f ./interface/stlink.cfg -f ./target/dp32g030.cfg -c "init; reset halt; uv_flash_bl bootloader.bin; shutdown"
pause