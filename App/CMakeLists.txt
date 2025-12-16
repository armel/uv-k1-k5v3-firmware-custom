
add_library(App INTERFACE)
target_include_directories(App INTERFACE
    .
    app
)
target_link_libraries(App INTERFACE PY32F071_Driver)
target_compile_definitions(App INTERFACE PRINTF_INCLUDE_CONFIG_H)

target_sources(App INTERFACE
    # Drivers
    driver/backlight.c
    driver/bk4829.c
    driver/py25q16.c
    driver/gpio.c
    driver/i2c.c
    driver/keyboard.c
    driver/st7565.c
    driver/system.c
    driver/systick.c
    # Main
    app/action.c
    app/app.c
    app/chFrScanner.c
    app/common.c
    app/dtmf.c
    app/generic.c
    app/main.c
    app/menu.c
    app/scanner.c
    audio.c
    bitmaps.c
    board.c
    dcs.c
    font.c
    frequencies.c
    functions.c
    helper/battery.c
    helper/boot.c
    misc.c
    radio.c
    scheduler.c
    settings.c
    ui/battery.c
    ui/helper.c
    ui/inputbox.c
    ui/main.c
    ui/menu.c
    ui/scanner.c
    ui/status.c
    ui/ui.c
    ui/welcome.c
    "version.c"
    main.c
    external/printf/printf.c
    app/selective_call.c
)


macro(enable_feature feature)
    if(${feature})
        target_compile_definitions(App INTERFACE ${feature})
        if(${ARGC} GREATER 1)
            target_sources(App INTERFACE ${ARGN})
        endif()
    endif()
endmacro()

# compile options (see README.md for descriptions)

if(ENABLE_FEAT_F4HWN)
    target_compile_definitions(App INTERFACE 
        "AUTHOR_STRING_1=\"${AUTHOR_STRING_1}\""
        "AUTHOR_STRING_2=\"${AUTHOR_STRING_2}\""
        "VERSION_STRING_1=\"${VERSION_STRING_1}\""
        "VERSION_STRING_2=\"${VERSION_STRING_2}\""
        "EDITION_STRING=\"${EDITION_STRING}\""
        ALERT_TOT=10
    )
endif()

target_compile_definitions(App INTERFACE 
    "AUTHOR_STRING=\"${AUTHOR_STRING}\""
    "VERSION_STRING=\"${VERSION_STRING}\""
)

if(NOT SQL_TONE)
    set(SQL_TONE 550) # For SA818, use 600 and python3 sa818.py --port /dev/ttyS2 radio --frequency 434.975 --ctcss 71.9 --tail Open
endif()
target_compile_definitions(App INTERFACE SQL_TONE=${SQL_TONE})

if(ENABLE_AIRCOPY OR ENABLE_UART OR ENABLE_USB)
    target_sources(App INTERFACE 
        driver/crc.c
        driver/eeprom_compat.c
    )
endif()

enable_feature(ENABLE_UART
    driver/uart.c 
    app/uart.c
    # driver/aes.c
)

if(ENABLE_USB)
    target_link_libraries(App INTERFACE CherryUSB)
    target_compile_definitions(App INTERFACE ENABLE_USB)
    target_include_directories(App INTERFACE usb)
    target_sources(App INTERFACE 
        driver/vcp.c
        usb/usbd_cdc_if.c
    )
endif()

if(ENABLE_UART OR ENABLE_USB)
    target_sources(App INTERFACE 
        app/uart.c
    )
endif()

# ---- STOCK QUANSHENG FEATURES ----

enable_feature(ENABLE_FMRADIO
    driver/bk1080.c 
    app/fm.c 
    ui/fmradio.c
)
enable_feature(ENABLE_AIRCOPY
    app/aircopy.c 
    ui/aircopy.c
)
enable_feature(ENABLE_NOAA)
enable_feature(ENABLE_VOICE 
    driver/voice.c
)
enable_feature(ENABLE_VOX)
enable_feature(ENABLE_ALARM)
enable_feature(ENABLE_TX1750)
enable_feature(ENABLE_PWRON_PASSWORD
    ui/lock.c
)
enable_feature(ENABLE_DTMF_CALLING)
enable_feature(ENABLE_FLASHLIGHT
    app/flashlight.c
)

# ---- CUSTOM MODS ----

enable_feature(ENABLE_SPECTRUM
    app/spectrum.c
)
enable_feature(ENABLE_BIG_FREQ)
enable_feature(ENABLE_SMALL_BOLD)
enable_feature(ENABLE_CUSTOM_MENU_LAYOUT)
enable_feature(ENABLE_KEEP_MEM_NAME)
enable_feature(ENABLE_WIDE_RX)
enable_feature(ENABLE_TX_WHEN_AM)
enable_feature(ENABLE_F_CAL_MENU)
enable_feature(ENABLE_CTCSS_TAIL_PHASE_SHIFT)
enable_feature(ENABLE_BOOT_BEEPS)
enable_feature(ENABLE_SHOW_CHARGE_LEVEL)
enable_feature(ENABLE_REVERSE_BAT_SYMBOL)
enable_feature(ENABLE_NO_CODE_SCAN_TIMEOUT)
enable_feature(ENABLE_AM_FIX
    am_fix.c
)
enable_feature(ENABLE_SQUELCH_MORE_SENSITIVE)
enable_feature(ENABLE_FASTER_CHANNEL_SCAN)
enable_feature(ENABLE_RSSI_BAR)
enable_feature(ENABLE_AUDIO_BAR)
enable_feature(ENABLE_COPY_CHAN_TO_VFO)
enable_feature(ENABLE_REDUCE_LOW_MID_TX_POWER)
enable_feature(ENABLE_BYP_RAW_DEMODULATORS)
enable_feature(ENABLE_BLMIN_TMP_OFF)
enable_feature(ENABLE_SCAN_RANGES)
enable_feature(ENABLE_NAVIG_LEFT_RIGHT)

# ---- CONTRIB MODS ----

# Thank you @markusb
enable_feature(ENABLE_REGA
    app/rega.c
) 
# Thank you @reppad
enable_feature(ENABLE_EXTRA_UART_CMD)

# ---- F4HWN MODS ----

enable_feature(ENABLE_FEAT_F4HWN)
enable_feature(ENABLE_FEAT_F4HWN_GAME 
    app/breakout.c
)
enable_feature(ENABLE_FEAT_F4HWN_SCREENSHOT 
    screenshot.c
)
enable_feature(ENABLE_FEAT_F4HWN_SPECTRUM)
enable_feature(ENABLE_FEAT_F4HWN_RX_TX_TIMER)
enable_feature(ENABLE_FEAT_F4HWN_CHARGING_C)
enable_feature(ENABLE_FEAT_F4HWN_SLEEP)
enable_feature(ENABLE_FEAT_F4HWN_RESUME_STATE)
enable_feature(ENABLE_FEAT_F4HWN_NARROWER)
enable_feature(ENABLE_FEAT_F4HWN_INV)
enable_feature(ENABLE_FEAT_F4HWN_CTR)
enable_feature(ENABLE_FEAT_F4HWN_RESCUE_OPS)
enable_feature(ENABLE_FEAT_F4HWN_VOL)
enable_feature(ENABLE_FEAT_F4HWN_RESET_CHANNEL)
enable_feature(ENABLE_FEAT_F4HWN_PMR)
enable_feature(ENABLE_FEAT_F4HWN_GMRS_FRS_MURS)
enable_feature(ENABLE_FEAT_F4HWN_CA)
enable_feature(ENABLE_FEAT_F4HWN_DEBUG)

# ---- DEBUGGING ----

enable_feature(ENABLE_AM_FIX_SHOW_DATA)
enable_feature(ENABLE_AGC_SHOW_DATA)
enable_feature(ENABLE_UART_RW_BK_REGS)

# ---- COMPILER/LINKER OPTIONS ----

enable_feature(ENABLE_SWD)
