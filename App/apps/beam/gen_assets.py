#!/usr/bin/env python3
# Beam read-only assets: status texts, the packet obfuscation key and the
# BK4819 FSK register sequences run by the app (APP_CAP_BEAM2).
#
#   ./gen_assets.py beam_assets.bin beam_assets.h
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

a = Assets("BEAM")
# Indexed by beam_app.c: 0/1 = READY in TX/RX mode, then status + 1.
a.table("T_STATE", ["BEAM TX", "BEAM RX", "SENDING", "SENT", "WAITING",
                    "RECEIVED", "MEM FULL", "ERROR"])
a.u16("OBFUSCATION", [0x6C16, 0xE614, 0x912E, 0x400D, 0x3521, 0x40D5, 0x0313, 0x80E9])

# Register sequences: (register, value) word pairs; register SEQ_DELAY means
# "wait value ms".  Each one mirrors the resident BK4819 driver routine noted.
SEQ_DELAY = 0xFF
a.const("SEQ_DELAY", SEQ_DELAY)

def seq(name, pairs):
    a.u16(name, [w for pair in pairs for w in pair])

RESET = [                       # BK4819_ResetFSK
    (0x3F, 0x0000),             # disable interrupts
    (0x59, 0x0068),             # sync length 4 bytes, 7 byte preamble
    (SEQ_DELAY, 30),
    (0x30, 0x0000),             # BK4819_Idle
]
seq("SEQ_RESET", RESET)
seq("SEQ_SETUP", [              # BK4819_SetupAircopy + BK4819_ResetFSK
    (0x70, 0x00C3),             # enable Tone2, tuning gain 48
    (0x72, 0x3065),             # Tone2 baudrate 1200
    (0x58, 0x00C1),             # FSK enable, 1.2K RX bandwidth, TX/RX FSK 1.2K
    (0x5C, 0x5665),             # enable CRC
    (0x5D, 0x4700),             # FSK data length 72 bytes
    (0x5E, 0x3204),
] + RESET)
seq("SEQ_RX", RESET + [         # BK4819_PrepareFSKReceive
    (0x02, 0x0000),
    (0x3F, 0x0000),
    (0x37, 0x9F1F),             # BK4819_RX_TurnOn
    (0x30, 0x0000),
    (0x30, 0xBFF1),
    (0x3F, 0x3000),             # FSK RX finished | FIFO almost full interrupts
    (0x59, 0x4068),             # clear RX FIFO
    (0x59, 0x3068),             # scramble + FSK RX enable
])
seq("SEQ_TX_LOAD", [            # BK4819_SendFSKData, before the FIFO fill
    (SEQ_DELAY, 30),
    (0x3F, 0x8000),             # FSK TX finished interrupt
    (0x59, 0x8068),             # clear TX FIFO
    (0x59, 0x0068),
])
seq("SEQ_TX_GO", [              # BK4819_SendFSKData, after the FIFO fill
    (SEQ_DELAY, 20),
    (0x59, 0x2868),             # FSK TX enable
])
seq("SEQ_TX_END", [             # BK4819_SendFSKData tail + PA bias off
    (0x02, 0x0000),
    (SEQ_DELAY, 30),
] + RESET + [
    (0x36, 0x0088),             # BK4819_SetupPowerAmplifier(0, 0)
])
a.main()
