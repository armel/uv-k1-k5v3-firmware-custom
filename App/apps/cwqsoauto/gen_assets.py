#!/usr/bin/env python3
"""CW QSO AUTO: QSO/settings labels, Morse models and unchanged RX profile."""

import os
import re
import struct
from pathlib import Path
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

USB_VARIANT = True
TITLE = "CW QSO Auto"

a = Assets("CWQSOAUTO")
if USB_VARIANT:
    a.table("T_VERSION", ["8 STRAIGHT", "8 AUTO", "8 IAMB A", "8 IAMB B", "", ""], stride=16)
else:
    a.text("T_VERSION", os.environ.get("APP_VER", "dev"))
a.text("T_DENIED", "TX OFF")
a.table("T_MODE", ["1 STRAIGHT", "1 PADDLE"])
a.table("T_ORDER", ["5 DI-DA", "5 DA-DI"])
a.text("T_SPEED", "2 WPM ")
a.text("T_RX_PITCH", "3 TONE ")
a.text("T_BREAKIN", "4 BK ")
a.text("T_XTAL", "6 CAL ")
# BK4829 register / value pairs that configureCwRx writes, in this order,
# to set the CW receive path.
a.u16("RX_PROFILE", [
    0x42, 0x6B5A,   # RX level
    0x2A, 0x7400,   # RX filter 1
    0x2B, 0x0000,   # RX filter 2
    0x2F, 0x9890,   # RX filter 3
    0x54, 0x9009,   # audio 1
    0x55, 0x31A9,   # audio 2
    0x3D, 0x0000,   # audio filter
])
# Normal-font QSO labels and the separate settings screen.
a.const("T_TITLE_CHARS", len(TITLE))
# Fixed number, space, editable ASCII name (0..12 bytes), terminating NUL.
# All 16 records have identical structure; CW payloads remain at offset 512.
message_names = ["CQ", "REPONSE", "73"] + [f"MSG {i}" for i in range(4, 15)] + ["AGN?", "?"]
a.const("MSG_NAME_BYTES", 12)
a.table("T_MESSAGES", [f"{i:02} {name}" for i, name in enumerate(message_names, 1)], stride=16)
a.text("T_SETTINGS", "CW SETTINGS")
a.text("T_SETTINGS_F", "CW F(-)")
a.text("T_RST", "RST ")
a.text("T_FREQ_TAIL", "0 Hz")
a.text("T_IDLE", "\0REPEAT: " + "\0" * 2)
a.text("IDLE_PADDING", "\0" * 5)
a.table("T_TX", [f"TX MSG {i:02}" for i in range(1, 17)], stride=16)
a.table("T_HINTS", ["UP/DN MSG MENU", "0-9 RST PTT TX", "S1 AGN? S2 ?"], stride=16)
a.text("T_TUNE", "UP/DN FREQ F:-")
a.const("MSG_BYTES", 64)
a.const("MSG_COUNT", 16)
a.const("MSG_HEADER", 4)
a.const("MSG_STRIDE", 68)

# One editable source of truth; never compile these strings into the overlay.
identity = (Path(__file__).parent / "qso_identity.h").read_text()
def station(name):
    return re.search(r'^#define ' + name + r' "([A-Z0-9/]+)"$', identity, re.M).group(1)
call = station("QSO_CALL").encode("ascii")
name = station("QSO_NAME").encode("ascii")
a.const("RST_TOKEN", 2)
# Fixed identity is folded at build time; only RST needs a runtime token.
messages = [b"CQ CQ CQ DE " + call + b" " + call + b" K\0",
            b"DE " + call + b" UR \x02 \x02 NAME " + name + b" HW?\0",
            b"73 TU E E\0"] + [f"MSG {i}".encode("ascii") + b"\0" for i in range(4, 15)] + [b"AGN?\0", b"?\0"]
assert len(messages) == 16 and all(len(message) <= 64 for message in messages)
assert all(message[-1] == 0 and b"  " not in message and not message.startswith(b" ")
           and not message.endswith(b" \0") for message in messages)
# Per-record signature: CW, format version 1, one-based message number.
# A complete record is one 68-byte CFG_PROXY block.
a.raw("MSG_MODELS", b"".join(b"CW" + bytes([1, i]) + message.ljust(64, b"\0")
                         for i, message in enumerate(messages, 1)))
patterns = {"A":".-","B":"-...","C":"-.-.","D":"-..","E":".","F":"..-.","G":"--.","H":"....","I":"..","J":".---","K":"-.-","L":".-..","M":"--","N":"-.","O":"---","P":".--.","Q":"--.-","R":".-.","S":"...","T":"-","U":"..-","V":"...-","W":".--","X":"-..-","Y":"-.--","Z":"--..","0":"-----","1":".----","2":"..---","3":"...--","4":"....-","5":".....","6":"-....","7":"--...","8":"---..","9":"----.","/":"-..-.","?":"..--.."}
def encode(pattern):
    code=1
    for el in reversed(pattern): code = code*2 + (el == "-")
    return code
a.u8("MORSE", [encode(patterns[chr(c)]) if chr(c) in patterns else 0 for c in range(ord('/'), ord('Z')+1)])
# MORSE begins at ASCII '/': MORSE + character - '/' folds to character.
# Spend cold Flash padding rather than instructions in the overlay.
a.prefix("MORSE_PAD", bytes(ord('/')))
item = next(item for item in a.data if item[0] == "MORSE")
a.data.remove(item)
a.prefixes.append(item)
# This long settings table otherwise pushes the compact field labels past 255.
cold_names = {"T_VERSION", "T_MESSAGES", "T_TX"}
cold = [item for item in a.texts if item[0] in cold_names]
a.texts = [item for item in a.texts if item[0] not in cold_names]
a.data.extend(cold)
# Stable public bank base; independent of labels and compiler versions.
_, layout, _ = a.build()
model_offset = next(offset for name, offset, _ in layout if name == "MSG_MODELS")
model_index = next(i for i, item in enumerate(a.data) if item[0] == "MSG_MODELS")
assert model_offset <= 512
a.raw("MODEL_PAD", bytes(512 - model_offset))
a.data.insert(model_index, a.data.pop())
_, fields, _ = a.build()
offsets = {name: offset for name, offset, length in fields}
# Reuse the MENU suffix of the existing hints; no added Flash string.
a.const("T_MENU", offsets["T_HINTS"] + len("UP/DN MSG "))
assert a.build()[0][offsets["T_HINTS"] + 10:offsets["T_HINTS"] + 15] == b"MENU\0"
config = [("T_MODE", 11), ("T_SPEED", 0), ("T_RX_PITCH", 0),
          ("T_BREAKIN", 0), ("T_ORDER", 8), ("T_XTAL", 0)]
assert all(offsets[name] < 256 for name, _ in config)
# Legacy config layout, checked against every C member at compilation.
ram_offsets = dict(WPM=1, MODE=2, REVERSE=3, CAL=4, PITCH=6, BREAK=8)
for name, offset in ram_offsets.items():
    a.const("CFG_" + name + "_OFFSET", offset)
display = [("MODE", False), ("WPM", False), ("PITCH", True),
           ("BREAK", True), ("REVERSE", False), ("CAL", True)]
assert all(not wide or ram_offsets[name] % 2 == 0 for name, wide in display)
a.u8("CONFIG_FIELDS", [value for i, ((label, stride), (name, wide)) in enumerate(zip(config, display))
                       for value in (offsets[label], stride, ram_offsets[name] | (128 if wide else 0),
                                     2 + (i % 2) * 64, 4 + i // 2, 61 + (i % 2) * 64)]
     + [offsets["T_IDLE"] + 1, 0, 30, 2, 2, 59, 0, 0, 0, 2, 3, 27])

# Read the existing C definitions rather than maintain a second set of defaults.
source = (Path(__file__).parent / "cwqsoauto_app.c").read_text()
def default(name):
    return int(re.search(r"^#define\s+" + name + r"\s+(0x[0-9A-Fa-f]+|\d+)", source, re.M).group(1), 0)
a.raw("DEFAULT_CONFIG", struct.pack("<BBBBhHHBB", default("CFG_MAGIC"),
      default("WPM_DEFAULT"), 0, 0, default("CAL_DEFAULT"),
      default("RX_PITCH_DEFAULT"), default("BREAKIN_DEFAULT"), 1, 0))

# Cold preview legend: keep the bank and existing settings offsets stable.
a.raw("T_PREVIEW_HELP", b"*=RST >=SUITE\0")

# Names-aware format marker. Contents keep their original CW/v1 records.
a.raw("NAME_FORMAT", b"QNM1")

a.main()
