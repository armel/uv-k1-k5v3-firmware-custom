#!/usr/bin/env bash
openocd -f ./interface/stlink.cfg -f ./target/dp32g030.cfg -c "init; reset halt; uv_flash_bl bootloader.bin; shutdown"