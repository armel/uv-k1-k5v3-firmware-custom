#!/usr/bin/env python3
# ToneCall assets: the firmware's CTCSS/DCS tables, to show the channel's TX
# tone. The presets are not assets: they live in the app's config area
# (tonecall_seq.h), empty until the user defines them.
#
#   ./gen_assets.py tonecall_assets.bin tonecall_assets.h
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

# The channel's TX tone, shown next to the frequency: the firmware's CTCSS
# (Hz x 10) and DCS tables, read from App/dcs.c so they cannot drift.
import re
dcs_c = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "dcs.c")).read()
def c_array(name):
    body = re.search(name + r"\[[^]]*\]\s*=\s*\{([^}]*)\}", dcs_c).group(1)
    return [int(v, 0) for v in re.findall(r"0[xX][0-9a-fA-F]+|\d+", body)]
ctcss = c_array("CTCSS_Options")
dcs_low = c_array("DCS_OptionsLow")
if len(ctcss) != 50 or len(dcs_low) != 104:
    sys.exit("App/dcs.c tables changed")

a = Assets("TONECALL")
a.u16("CTCSS", ctcss)
a.u8("DCS_LOW", dcs_low)     # DCS code = DCS_LOW[i] | 0x100 from index 64
a.const("CTCSS_COUNT", len(ctcss))
a.const("DCS_COUNT", len(dcs_low))
a.main()
