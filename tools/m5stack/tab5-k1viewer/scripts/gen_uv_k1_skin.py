#!/usr/bin/env python3
#
# Copyright 2026 Armel F4HWN
# https://github.com/armel
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
"""Generate the 720x1280 UV-K1 front face used by the Tab5 portrait viewer.

Outputs, relative to the tab5-k1viewer folder:
  assets/uv-k1-720x1280.svg   showcase render, LCD filled from a firmware screenshot
  assets/uv-k1-skin.svg       firmware background: blank glass, RX/TX LED off
  assets/k1-connect-icon.svg  "connect the radio" icon, shown on black when no K1
  assets/k1-connect-text.svg  "connect the radio" message under the icon
  main/uv_k1_skin_layout.h    glass, LCD, LED, key and connect-screen geometry
  main/uv_k1_skin_images.cmake  images embedded by the build, with their sizes

With --render, the SVGs are also rendered to PNG with headless Chrome. The
firmware build converts the PNGs to RGB565 (png_to_rgb565.py).
"""

import argparse
import math
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
REPO_DIR = PROJECT_DIR.parent.parent.parent
DEFAULT_SCREENSHOT = REPO_DIR / 'images' / 'PS_DW_OP_LOCK_PERCENT_BAT.png'
CHROME_CANDIDATES = (
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    'google-chrome',
    'chromium',
    'chromium-browser',
)

W, H = 720, 1280

# ---- geometry (derived from product photos, 1.75 px per photo px) ----
# Local coordinates; the whole radio is drawn shifted down by DY so that it
# ends at the bottom of the screen, the antenna filling the top.
DY = 50
BODY_L, BODY_R = 10, 710
PANEL_TOP = 160
PANEL_BOT = 1170                      # flat grey face ends ~half a key pitch below row 4
CHAMFER_BOT = 1206                    # bottom chamfer of the grey face
BODY_BOT = 1220                       # black housing lip (short battery: nothing clipped below)
RAIL_W = 54
BEZEL = (115, 272, 490, 360)          # x, y, w, h
GLASS = (158, 302, 404, 304)
LCD = (168, 310, 384, 288)            # 128x64 LCD pixels at 3 x 4.5 (real 1:1.5 pixel aspect)
LCD_CELL_W = 3
LCD_ROW_NUM, LCD_ROW_DEN = 2, 9       # LCD row = screen row * 2 / 9 (4.5 screen rows each)
PLATE_Y = 662
MIC_Y = 748
KEY_X0, KEY_PITCH, KEY_W = 112, 129, 108
ROW1_Y, ROW1_H = 769, 79
NUM_Y0, NUM_PITCH, NUM_H = 879, 96, 65
KEY_RADIUS = 13
# Side keys on the left rail, protruding past the housing like on the radio.
# The PTT is only drawn: the firmware refuses PTT over serial. F1 and F2 get
# touch areas much wider than the drawn keys, up to the keypad.
SIDE_KEY_X, SIDE_KEY_W, SIDE_KEY_RADIUS = 2, 40, 14
SIDE_KEYS = [                         # label, firmware code, y, h
    ('PTT', None, 370, 224),
    ('F1', 0x12, 700, 76),            # KEY_SIDE1
    ('F2', 0x11, 810, 76),            # KEY_SIDE2
]
SIDE_HIT_W = 106                      # touch areas reach the first column of keys
SIDE_HIT_MARGIN = 15
LED_TOP_Y, LED_BOT_Y = 100.0, 121.0   # RX/TX light pipe at the antenna base
LED_TOP_X = (143.0, 191.0)
LED_BOT_X = (133.4, 200.6)
LED_GLOW = 10
TORCH_X, TORCH_Y = 352.0, 96.0       # white torch LED dome on top, between antenna and knob
TORCH_RX, TORCH_RY = 17.0, 6.5
TORCH_TOP = 94                        # top edge of its housing, which hides the lower part
TORCH_HALO = 24
TORCH_BEAM_SPREAD = 1.0               # beam half-width growth per pixel of rise
CONNECT_ICON = (160, 360, 400, 360)  # screen coordinates of the "connect" icon and message
CONNECT_TEXT = (60, 760, 600, 130)
CONNECT_BLUE = '#3d93e6'
CONNECT_METAL = '#c9cdd3'

# Firmware key codes (App/driver/keyboard.h). On the K1, '<' and '>' sit in the
# KEY_UP and KEY_DOWN positions of the key matrix.
KEY_ROWS = [
    [('MENU', 0x0A), ('<', 0x0B), ('>', 0x0C), ('EXIT', 0x0D)],
    [('1', 0x01), ('2', 0x02), ('3', 0x03), ('*', 0x0E)],
    [('4', 0x04), ('5', 0x05), ('6', 0x06), ('0', 0x00)],
    [('7', 0x07), ('8', 0x08), ('9', 0x09), ('F', 0x0F)],
]

FONT = "font-family=\"'Helvetica Neue',Helvetica,Arial,sans-serif\""
LBL = 'fill="#f5f6f7"'

DEFS = '''<defs>
  <radialGradient id="bg" cx="50%" cy="40%" r="75%">
    <stop offset="0" stop-color="#2a2d31"/><stop offset="1" stop-color="#0c0d0f"/>
  </radialGradient>
  <linearGradient id="cyl" x1="0" x2="1" y1="0" y2="0">
    <stop offset="0" stop-color="#0b0c0d"/><stop offset=".22" stop-color="#3a3c40"/>
    <stop offset=".45" stop-color="#1c1d20"/><stop offset="1" stop-color="#08090a"/>
  </linearGradient>
  <linearGradient id="blueCap" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#5aa8f0"/><stop offset=".5" stop-color="#2b7fd6"/>
    <stop offset="1" stop-color="#1655a3"/>
  </linearGradient>
  <linearGradient id="blueSide" x1="0" x2="1" y1="0" y2="0">
    <stop offset="0" stop-color="#1a5fb4"/><stop offset=".5" stop-color="#3d93e6"/>
    <stop offset="1" stop-color="#174f96"/>
  </linearGradient>
  <linearGradient id="shoulder" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#3a3c40"/><stop offset=".25" stop-color="#1f2023"/>
    <stop offset="1" stop-color="#121315"/>
  </linearGradient>
  <linearGradient id="panelH" x1="0" x2="1" y1="0" y2="0">
    <stop offset="0" stop-color="#3d4044"/><stop offset=".06" stop-color="#686b71"/>
    <stop offset=".35" stop-color="#73767c"/><stop offset=".7" stop-color="#6a6d73"/>
    <stop offset=".94" stop-color="#5d6065"/><stop offset="1" stop-color="#383b3f"/>
  </linearGradient>
  <linearGradient id="panelV" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#ffffff" stop-opacity=".10"/>
    <stop offset=".5" stop-color="#ffffff" stop-opacity="0"/>
    <stop offset="1" stop-color="#000000" stop-opacity=".18"/>
  </linearGradient>
  <linearGradient id="railL" x1="0" x2="1" y1="0" y2="0">
    <stop offset="0" stop-color="#0d0e0f"/><stop offset=".55" stop-color="#26282b"/>
    <stop offset="1" stop-color="#151618"/>
  </linearGradient>
  <linearGradient id="railR" x1="1" x2="0" y1="0" y2="0">
    <stop offset="0" stop-color="#0d0e0f"/><stop offset=".55" stop-color="#26282b"/>
    <stop offset="1" stop-color="#151618"/>
  </linearGradient>
  <linearGradient id="bezel" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#2a2c2f"/><stop offset=".12" stop-color="#151618"/>
    <stop offset="1" stop-color="#0b0c0d"/>
  </linearGradient>
  <linearGradient id="glass" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#dfe3dd"/><stop offset=".5" stop-color="#eef1ec"/>
    <stop offset="1" stop-color="#d9ddd6"/>
  </linearGradient>
  <linearGradient id="gloss" x1="0" x2="1" y1="0" y2="1">
    <stop offset="0" stop-color="#ffffff" stop-opacity=".35"/>
    <stop offset=".45" stop-color="#ffffff" stop-opacity=".06"/>
    <stop offset=".46" stop-color="#ffffff" stop-opacity="0"/>
    <stop offset="1" stop-color="#ffffff" stop-opacity="0"/>
  </linearGradient>
  <linearGradient id="plate" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#3b3d41"/><stop offset="1" stop-color="#26272a"/>
  </linearGradient>
  <linearGradient id="key" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#34363a"/><stop offset=".18" stop-color="#1d1e21"/>
    <stop offset=".85" stop-color="#141517"/><stop offset="1" stop-color="#0b0b0c"/>
  </linearGradient>
  <linearGradient id="well" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#2a2b2e"/><stop offset="1" stop-color="#4a4c51"/>
  </linearGradient>
  <radialGradient id="ledGreen" cx="50%" cy="40%" r="65%">
    <stop offset="0" stop-color="#eaffef"/><stop offset=".35" stop-color="#7dff9c"/>
    <stop offset="1" stop-color="#16c94a"/>
  </radialGradient>
  <radialGradient id="torchLens" cx="45%" cy="40%" r="60%">
    <stop offset="0" stop-color="#ffffff"/><stop offset=".55" stop-color="#f2f5f8"/>
    <stop offset="1" stop-color="#aab2bb"/>
  </radialGradient>
  <linearGradient id="rubber" x1="0" x2="1" y1="0" y2="0">
    <stop offset="0" stop-color="#0c0d0e"/><stop offset=".45" stop-color="#34373b"/>
    <stop offset="1" stop-color="#1a1b1e"/>
  </linearGradient>
  <linearGradient id="chamfer" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#8d9096"/><stop offset=".25" stop-color="#5f6267"/>
    <stop offset="1" stop-color="#3f4246"/>
  </linearGradient>
  <filter id="glow" x="-80%" y="-150%" width="260%" height="400%">
    <feGaussianBlur stdDeviation="6"/>
  </filter>
  <filter id="drop" x="-10%" y="-10%" width="120%" height="130%">
    <feDropShadow dx="0" dy="3" stdDeviation="2.5" flood-color="#000" flood-opacity=".55"/>
  </filter>
  <filter id="soft" x="-5%" y="-5%" width="110%" height="110%">
    <feDropShadow dx="0" dy="10" stdDeviation="10" flood-color="#000" flood-opacity=".6"/>
  </filter>
</defs>'''


def read_png_rgb(path):
    """Decode an 8-bit RGB or RGBA, non-interlaced PNG into rows of RGB bytes."""
    data = Path(path).read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG file')
    pos, idat = 8, bytearray()
    width = height = channels = 0
    while pos < len(data):
        length, kind = struct.unpack('>I4s', data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            width, height, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', chunk)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f'{path}: only 8-bit RGB/RGBA non-interlaced PNGs are supported')
            channels = 3 if ctype == 2 else 4
        elif kind == b'IDAT':
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    rows, prev = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        bpp = channels
        if kind == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif kind == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif kind == 3:
            for x in range(stride):
                left = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((left + prev[x]) >> 1)) & 255
        elif kind == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 255
        rows.append(line)
        prev = line
    if channels == 4:
        rows = [bytearray(b for i in range(0, stride, 4) for b in row[i:i + 3]) for row in rows]
    return width, height, rows


def lcd_path(screenshot):
    """Build an SVG path of the lit LCD pixels, one unit per LCD pixel."""
    # Firmware screenshots are 256x192: each LCD pixel is 2x3 image pixels.
    w, h, rows = read_png_rgb(screenshot)
    sx, sy = w // 128, h // 64
    d = []
    for y in range(64):
        row = rows[y * sy + sy // 2]

        def lit(x):
            return sum(row[x * sx * 3:x * sx * 3 + 3]) < 384

        x = 0
        while x < 128:
            if lit(x):
                x0 = x
                while x < 128 and lit(x):
                    x += 1
                d.append(f'M{x0} {y}h{x - x0}v1h{x0 - x}z')
            else:
                x += 1
    return ''.join(d)


def led_path():
    return (f'M{LED_TOP_X[0]:g} {LED_TOP_Y:g} H{LED_TOP_X[1]:g} '
            f'L{LED_BOT_X[1]:g} {LED_BOT_Y:g} H{LED_BOT_X[0]:g} Z')


def key_columns():
    return [KEY_X0 + i * KEY_PITCH for i in range(4)]


def key_rects():
    """Yield (label, code, x, y, w, h) in local coordinates."""
    for r, row in enumerate(KEY_ROWS):
        y = ROW1_Y if r == 0 else NUM_Y0 + (r - 1) * NUM_PITCH
        h = ROW1_H if r == 0 else NUM_H
        for c, (label, code) in enumerate(row):
            yield label, code, key_columns()[c], y, KEY_W, h


def side_key_hits():
    """Yield (label, code, y, h, hit_y, hit_h) for the side keys that can be touched."""
    keys = [key for key in SIDE_KEYS if key[1] is not None]
    for i, (label, code, y, h) in enumerate(keys):
        top = y - SIDE_HIT_MARGIN if i == 0 else (keys[i - 1][2] + keys[i - 1][3] + y) // 2
        bottom = y + h + SIDE_HIT_MARGIN if i == len(keys) - 1 else (y + h + keys[i + 1][2]) // 2
        yield label, code, y, h, top, bottom - top


def build_svg(lcd, led_lit):
    out = []
    o = out.append

    o(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">')
    o('<title>Quansheng UV-K1</title>')
    o(DEFS)
    o(f'<rect width="{W}" height="{H}" fill="url(#bg)"/>')
    o(f'<g transform="translate(0 {DY})">')

    # ---- antenna (left) ----
    o('<g id="antenna">')
    o(f'<rect x="117" y="{-DY - 10}" width="100" height="{DY - 18}" fill="url(#cyl)"/>')
    o('<path d="M117 -30 L91 -16 V60 H243 V-16 L217 -30 Z" fill="url(#cyl)"/>')
    o('<rect x="153" y="-8" width="28" height="54" rx="8" fill="#26282b" stroke="#0a0a0b" stroke-width="1.5"/>')
    o('<text x="167" y="19" font-family="Arial,sans-serif" font-size="15" font-weight="700" fill="#55585d" '
      'text-anchor="middle" dominant-baseline="central" transform="rotate(-90 167 19)">A1</text>')
    for y in range(28, 56, 9):
        o(f'<rect x="91" y="{y}" width="152" height="2" fill="#000" opacity=".35"/>')
    o('<rect x="76" y="54" width="182" height="54" rx="12" fill="url(#cyl)"/>')
    o('<rect x="76" y="54" width="182" height="4" rx="2" fill="#5a5d62" opacity=".6"/>')
    o('<rect x="66" y="102" width="202" height="20" rx="6" fill="url(#cyl)"/>')
    o('</g>')

    # ---- RX/TX status LED: light pipe at the antenna base (green RX, red TX) ----
    o('<g id="led-rxtx">')
    if led_lit:
        o(f'<path d="{led_path()}" fill="#35ff6b" opacity=".75" filter="url(#glow)"/>')
        o(f'<path d="{led_path()}" fill="url(#ledGreen)" stroke="#0f6b2a" stroke-width="1.2"/>')
        o('<path d="M145.5 102.5 H188.5" stroke="#ffffff" stroke-width="2" opacity=".7" stroke-linecap="round"/>')
    else:
        # Unlit light pipe: the firmware draws the green/red glow over it.
        o(f'<path d="{led_path()}" fill="#18231b" stroke="#0a120c" stroke-width="1.2"/>')
        o('<path d="M145.5 102.5 H188.5" stroke="#9fb8a6" stroke-width="2" opacity=".35" stroke-linecap="round"/>')
    o('</g>')

    # ---- volume / power knob (right) ----
    o('<g id="knob">')
    o('<rect x="430" y="104" width="254" height="24" rx="8" fill="url(#cyl)"/>')
    o('<rect x="439" y="34" width="236" height="78" rx="14" fill="url(#cyl)"/>')
    for x in range(452, 664, 10):
        o(f'<rect x="{x}" y="46" width="3" height="60" fill="#000" opacity=".45"/>')
    o('<rect x="439" y="58" width="22" height="34" rx="6" fill="url(#blueSide)"/>')
    o('<rect x="653" y="58" width="22" height="34" rx="6" fill="url(#blueSide)"/>')
    o('<path d="M447 30 Q447 8 470 8 H644 Q667 8 667 30 V44 H447 Z" fill="url(#blueCap)"/>')
    o('<path d="M545 8 H569 L564 62 Q557 70 550 62 Z" fill="#1d63b8"/>')
    o('<path d="M549 8 H565 L561 52 Q557 58 553 52 Z" fill="url(#blueCap)"/>')
    o('<path d="M455 14 Q470 10 520 11" stroke="#bfe0ff" stroke-width="3" fill="none" opacity=".6" stroke-linecap="round"/>')
    o('</g>')

    # ---- top shoulder of the housing and white torch LED ----
    o('<path d="M22 176 V146 Q22 118 52 118 H668 Q698 118 698 146 V176 Z" fill="url(#shoulder)"/>')
    o('<path d="M52 119 H668" stroke="#55585d" stroke-width="2" opacity=".7"/>')
    tx, ty, top = TORCH_X, TORCH_Y, TORCH_TOP
    o('<g id="torch">')
    o(f'<ellipse cx="{tx:g}" cy="{ty - 5:g}" rx="{TORCH_RX + 3:g}" ry="5" fill="#ffffff" opacity=".3" filter="url(#glow)"/>')
    o(f'<ellipse cx="{tx:g}" cy="{ty:g}" rx="{TORCH_RX:g}" ry="{TORCH_RY:g}" fill="url(#torchLens)" '
      'stroke="#6d747c" stroke-width="1.2"/>')
    o(f'<ellipse cx="{tx - 5:g}" cy="{ty - 4.2:g}" rx="5" ry="1.4" fill="#ffffff"/>')
    o(f'<path d="M{tx - 34:g} 120 V{top + 10} Q{tx - 34:g} {top} {tx - 24:g} {top} H{tx + 24:g} '
      f'Q{tx + 34:g} {top} {tx + 34:g} {top + 10} V120 Z" fill="url(#cyl)"/>')
    o('</g>')

    # ---- grey front panel ----
    panel = (f'M{BODY_L} {PANEL_TOP + 44} Q{BODY_L} {PANEL_TOP} {BODY_L + 44} {PANEL_TOP} '
             f'H{BODY_R - 44} Q{BODY_R} {PANEL_TOP} {BODY_R} {PANEL_TOP + 44} V{CHAMFER_BOT} H{BODY_L} Z')
    o(f'<path d="M{BODY_L} 600 H{BODY_R} V{BODY_BOT - 30} Q{BODY_R} {BODY_BOT} {BODY_R - 30} {BODY_BOT} '
      f'H{BODY_L + 30} Q{BODY_L} {BODY_BOT} {BODY_L} {BODY_BOT - 30} Z" fill="#111214"/>')
    o(f'<g filter="url(#soft)"><path d="{panel}" fill="url(#panelH)"/></g>')
    o(f'<path d="{panel}" fill="url(#panelV)"/>')
    o(f'<path d="M{BODY_L + 44} {PANEL_TOP + 1.5} H{BODY_R - 44}" stroke="#b9bcc1" stroke-width="2" opacity=".6"/>')

    # ---- black side rails ----
    rl = BODY_L + RAIL_W
    rr = BODY_R - RAIL_W
    o(f'<path d="M{BODY_L} 330 Q{BODY_L} 292 {BODY_L + 30} 292 Q{rl} 292 {rl} 330 V{BODY_BOT} H{BODY_L + 30} '
      f'Q{BODY_L} {BODY_BOT} {BODY_L} {BODY_BOT - 30} Z" fill="url(#railL)"/>')
    o(f'<path d="M{BODY_R} 330 Q{BODY_R} 292 {BODY_R - 30} 292 Q{rr} 292 {rr} 330 V{BODY_BOT} H{BODY_R - 30} '
      f'Q{BODY_R} {BODY_BOT} {BODY_R} {BODY_BOT - 30} Z" fill="url(#railR)"/>')
    o(f'<path d="M{rl + 1.5} 328 V{CHAMFER_BOT}" stroke="#9a9da2" stroke-width="2" opacity=".45"/>')
    o(f'<path d="M{rr - 1.5} 328 V{CHAMFER_BOT}" stroke="#2f3134" stroke-width="2" opacity=".6"/>')

    # ---- side keys: dotted PTT, then F1 and F2 ----
    for label, code, y, h in SIDE_KEYS:
        o(f'<rect x="{SIDE_KEY_X}" y="{y}" width="{SIDE_KEY_W}" height="{h}" rx="{SIDE_KEY_RADIUS}" '
          'fill="url(#rubber)" stroke="#050506" stroke-width="1.5"/>')
        o(f'<rect x="{SIDE_KEY_X + 6}" y="{y + 6}" width="3" height="{h - 12}" rx="1.5" fill="#6d7177" opacity=".55"/>')
        if code is None:
            for i in range((h - 40) // 18):
                for dx in (17, 29):
                    o(f'<circle cx="{dx}" cy="{y + 26 + i * 18}" r="3" fill="#0a0a0b" stroke="#4a4d52" stroke-width="1"/>')
        else:
            cy = y + h / 2
            o(f'<text x="54" y="{cy:g}" {FONT} font-size="15" font-weight="700" fill="#9aa0a8" text-anchor="middle" '
              f'dominant-baseline="central" transform="rotate(-90 54 {cy:g})">{label}</text>')

    # ---- QUANSHENG logo plate ----
    o('<rect x="155" y="180" width="410" height="76" rx="6" fill="#0b0b0c" stroke="#3a3c40" stroke-width="1.5"/>')
    o('<g fill="none" stroke="#f4f5f6" stroke-width="5" stroke-linecap="round">')
    for r in (11, 20, 29):
        a0, a1 = math.radians(195), math.radians(262)
        x0, y0 = 236 + r * math.cos(a0), 236 + r * math.sin(a0)
        x1, y1 = 236 + r * math.cos(a1), 236 + r * math.sin(a1)
        o(f'<path d="M{x0:.1f} {y0:.1f} A{r} {r} 0 0 1 {x1:.1f} {y1:.1f}"/>')
    o('</g>')
    o(f'<text x="236" y="236" {FONT} font-size="44" font-weight="900" font-style="italic" '
      'fill="#f4f5f6" textLength="300" lengthAdjust="spacingAndGlyphs">QUAN<tspan dx="6">SHENG</tspan></text>')

    # ---- screen bezel and LCD ----
    bx, by, bw, bh = BEZEL
    gx, gy, gw, gh = GLASS
    lx, ly, lw, lh = LCD
    o(f'<rect x="{bx - 2}" y="{by + 2}" width="{bw + 4}" height="{bh + 4}" rx="36" fill="#000" opacity=".35"/>')
    o(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="34" fill="url(#bezel)"/>')
    o(f'<rect x="{bx + 11}" y="{by + 11}" width="{bw - 22}" height="{bh - 22}" rx="26" fill="none" '
      'stroke="#2c2e31" stroke-width="2"/>')
    o(f'<rect x="{gx - 6}" y="{gy - 6}" width="{gw + 12}" height="{gh + 12}" rx="6" fill="#050506"/>')
    o(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="3" fill="url(#glass)"/>')
    if lcd:
        sx, sy = lw / 128, lh / 64
        o(f'<path transform="translate({lx + 1} {ly + 2}) scale({sx:g} {sy:g})" d="{lcd}" fill="#000" opacity=".13"/>')
        o(f'<path transform="translate({lx} {ly}) scale({sx:g} {sy:g})" d="{lcd}" fill="#17191b"/>')
        o(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="3" fill="url(#gloss)"/>')

    # ---- "Mini Kong" and "MIC" plates ----
    for x, w, label in ((129, 254, 'Mini Kong'), (482, 114, 'MIC')):
        o(f'<rect x="{x - 3}" y="{PLATE_Y - 3}" width="{w + 6}" height="55" rx="16" fill="#18191b"/>')
        o(f'<rect x="{x}" y="{PLATE_Y}" width="{w}" height="49" rx="14" fill="url(#plate)"/>')
        o(f'<rect x="{x + 8}" y="{PLATE_Y + 1.5}" width="{w - 16}" height="2" rx="1" fill="#6a6d72" opacity=".6"/>')
        cx = x + w / 2
        o(f'<text x="{cx}" y="{PLATE_Y + 34}" {FONT} font-size="25" font-weight="700" text-anchor="middle" '
          f'fill="#55585d">{label}</text>')
        o(f'<text x="{cx}" y="{PLATE_Y + 32.5}" {FONT} font-size="25" font-weight="700" text-anchor="middle" '
          f'fill="#1b1c1e">{label}</text>')

    # ---- microphone hole ----
    o(f'<circle cx="539" cy="{MIC_Y}" r="8" fill="#5a5d62"/>')
    o(f'<circle cx="539" cy="{MIC_Y - 1}" r="6" fill="#09090a"/>')

    # ---- keypad ----
    def key_shape(x, y, w, h):
        o(f'<rect x="{x - 5}" y="{y - 5}" width="{w + 10}" height="{h + 10}" rx="17" fill="url(#well)"/>')
        o(f'<g filter="url(#drop)"><rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{KEY_RADIUS}" '
          'fill="url(#key)" stroke="#050506" stroke-width="1.5"/></g>')
        o(f'<rect x="{x + 10}" y="{y + 2.5}" width="{w - 20}" height="2" rx="1" fill="#8a8d92" opacity=".55"/>')

    def slit(cx, cy):
        o(f'<rect x="{cx - 26}" y="{cy - 4.5}" width="52" height="9" rx="4.5" fill="#1a1b1d"/>')
        o(f'<rect x="{cx - 24}" y="{cy + 3.5}" width="48" height="1.5" rx=".75" fill="#a4a7ac" opacity=".5"/>')

    cols = key_columns()

    # Row 1: MENU < > EXIT
    for i, x in enumerate(cols):
        key_shape(x, ROW1_Y, KEY_W, ROW1_H)
        cx, cy = x + KEY_W / 2, ROW1_Y + ROW1_H / 2
        if i in (0, 3):
            word = 'MENU' if i == 0 else 'EXIT'
            o(f'<text x="{cx}" y="{cy + 10}" {FONT} font-size="27" font-weight="700" text-anchor="middle" '
              f'{LBL}>{word}</text>')
        else:
            d = -1 if i == 1 else 1
            o(f'<path d="M{cx - 8 * d} {cy - 18} L{cx + 9 * d} {cy} L{cx - 8 * d} {cy + 18}" fill="none" '
              'stroke="#f5f6f7" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"/>')

    # Number rows: (digit, upper label, lower label)
    rows = [
        [('1', 'Band', ',.?'), ('2', 'A/B', 'abc'), ('3', 'V/M', 'def'), ('*', 'Scan', None)],
        [('4', 'FC', 'ghi'), ('5', 'WX', 'jkl'), ('6', 'H/L', 'mno'), ('0', None, 'FM')],
        [('7', 'VOX', 'pqrs'), ('8', 'R', 'tuv'), ('9', 'Call', 'wxyz'), ('#', 'KEY', 'F')],
    ]
    slit_rows = [ROW1_Y + ROW1_H + 15] + [NUM_Y0 + r * NUM_PITCH + NUM_H + 15 for r in range(2)]
    for sy_ in slit_rows:
        for x in cols:
            slit(x + KEY_W / 2, sy_)

    for r, row in enumerate(rows):
        y = NUM_Y0 + r * NUM_PITCH
        for c, (digit, up, low) in enumerate(row):
            x = cols[c]
            key_shape(x, y, KEY_W, NUM_H)
            dx, cy = x + 31, y + NUM_H / 2
            lx_ = x + 75
            if digit == '*':
                for a in (90, 30, 150):
                    ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
                    o(f'<path d="M{dx - 13 * ca:.1f} {cy + 2 - 13 * sa:.1f} L{dx + 13 * ca:.1f} {cy + 2 + 13 * sa:.1f}" '
                      'stroke="#f5f6f7" stroke-width="6.5" stroke-linecap="round"/>')
            else:
                size = 42 if digit == '#' else 44
                o(f'<text x="{dx}" y="{cy + 16}" {FONT} font-size="{size}" font-weight="700" text-anchor="middle" '
                  f'{LBL}>{digit}</text>')
            if digit == '1':
                o(f'<text x="{x + 54}" y="{y + 28}" {FONT} font-size="19" font-weight="500" {LBL}>Band</text>')
                o(f'<text x="{x + 55}" y="{y + 49}" {FONT} font-size="19" font-weight="500" {LBL}>,.?</text>')
            elif digit == '0':
                o(f'<text x="{lx_}" y="{cy + 8}" {FONT} font-size="22" font-weight="500" text-anchor="middle" '
                  f'{LBL}>FM</text>')
            elif digit == '*':
                o(f'<text x="{lx_ + 1}" y="{y + 28}" {FONT} font-size="19" font-weight="500" text-anchor="middle" '
                  f'{LBL}>Scan</text>')
                # Half arrow under "Scan": shaft to the left, single barb going up.
                o(f'<path d="M{lx_ + 20} {y + 45} H{lx_ - 19} L{lx_ - 11} {y + 38}" fill="none" stroke="#f5f6f7" '
                  'stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"/>')
            elif digit == '#':
                # Key-lock glyph: upright oval ring on the right, shaft to the left
                # ending in a single bit that rises.
                kx, ky = lx_, y + 23
                o(f'<g stroke="#f5f6f7" stroke-width="3" fill="none" stroke-linecap="round">'
                  f'<ellipse cx="{kx + 10}" cy="{ky}" rx="4.2" ry="6.2"/>'
                  f'<path d="M{kx + 5.8} {ky} H{kx - 12} V{ky - 6.5}"/></g>')
                o(f'<text x="{lx_}" y="{y + 52}" {FONT} font-size="21" font-weight="500" text-anchor="middle" '
                  f'{LBL}>F</text>')
            else:
                o(f'<text x="{lx_}" y="{y + 28}" {FONT} font-size="19" font-weight="500" text-anchor="middle" '
                  f'{LBL}>{up}</text>')
                o(f'<text x="{lx_}" y="{y + 49}" {FONT} font-size="19" font-weight="500" text-anchor="middle" '
                  f'{LBL}>{low}</text>')
            if digit == '5':
                # Tactile nub on the 5 key.
                o(f'<circle cx="{x + KEY_W - 17}" cy="{y + 11}" r="3.2" fill="#2c2e31" stroke="#6b6e73" stroke-width="1"/>')

    # ---- bottom of the face: chamfer and black housing lip (short battery) ----
    o(f'<rect x="{rl}" y="{PANEL_BOT}" width="{rr - rl}" height="{CHAMFER_BOT - PANEL_BOT}" fill="url(#chamfer)"/>')
    o(f'<rect x="{rl}" y="{PANEL_BOT}" width="{rr - rl}" height="2" fill="#c3c6cb" opacity=".55"/>')
    o(f'<rect x="{rl}" y="{CHAMFER_BOT - 2}" width="{rr - rl}" height="2" fill="#000" opacity=".5"/>')
    o(f'<path d="M{rl} {CHAMFER_BOT} H{rr} V{BODY_BOT} H{rl} Z" fill="#141517"/>')

    o('</g>')
    o('</svg>')
    return '\n'.join(out) + '\n'


def build_connect_icon_svg():
    """Stylised UV-K1 with a USB cable heading for the Tab5, on black."""
    _, _, w, h = CONNECT_ICON
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',
           '''<defs>
  <linearGradient id="iCyl" x1="0" x2="1" y1="0" y2="0">
    <stop offset="0" stop-color="#0b0c0d"/><stop offset=".25" stop-color="#3a3c40"/>
    <stop offset=".5" stop-color="#1c1d20"/><stop offset="1" stop-color="#08090a"/>
  </linearGradient>
  <linearGradient id="iFace" x1="0" x2="1" y1="0" y2="0">
    <stop offset="0" stop-color="#4b4e53"/><stop offset=".3" stop-color="#7c7f85"/>
    <stop offset=".75" stop-color="#6d7076"/><stop offset="1" stop-color="#45484c"/>
  </linearGradient>
</defs>''',
           f'<rect width="{w}" height="{h}" fill="#000"/>']
    o = out.append
    # Antenna (whip, A1 section, collar), blue-capped knob and torch.
    o('<rect x="70" y="4" width="24" height="44" rx="12" fill="url(#iCyl)"/>')
    o('<rect x="63" y="34" width="38" height="34" rx="6" fill="url(#iCyl)"/>')
    o('<rect x="56" y="62" width="52" height="16" rx="5" fill="url(#iCyl)"/>')
    o('<rect x="142" y="46" width="54" height="30" rx="6" fill="url(#iCyl)"/>')
    for x in range(148, 192, 6):
        o(f'<rect x="{x}" y="52" width="2" height="20" fill="#000" opacity=".5"/>')
    o('<rect x="142" y="56" width="5" height="11" rx="2" fill="#3d93e6"/>')
    o('<rect x="191" y="56" width="5" height="11" rx="2" fill="#3d93e6"/>')
    o('<path d="M144 52 Q144 40 156 40 H182 Q194 40 194 52 Z" fill="#2b7fd6"/>')
    o('<ellipse cx="124" cy="73" rx="8" ry="3.5" fill="#f2f5f8"/>')
    o('<rect x="113" y="73" width="22" height="7" rx="2" fill="#1b1d20"/>')
    # Housing: shoulder, grey face, black rails and side keys.
    o('<rect x="44" y="76" width="162" height="22" rx="10" fill="#1b1d20"/>')
    o('<rect x="40" y="88" width="170" height="258" rx="16" fill="url(#iFace)"/>')
    o('<rect x="40" y="124" width="13" height="222" rx="6" fill="#1b1d20"/>')
    o('<rect x="197" y="124" width="13" height="222" rx="6" fill="#1b1d20"/>')
    for y, kh in ((142, 58), (214, 18), (238, 18)):
        o(f'<rect x="35" y="{y}" width="9" height="{kh}" rx="4" fill="#2a2c30" stroke="#050506"/>')
    # Logo plate, bezel with the screen off, plates.
    o('<rect x="64" y="98" width="122" height="20" rx="3" fill="#0b0b0c"/>')
    o(f'<text x="125" y="112.5" {FONT} font-size="11.5" font-weight="900" font-style="italic" fill="#f4f5f6" '
      'text-anchor="middle" textLength="94" lengthAdjust="spacingAndGlyphs">QUANSHENG</text>')
    o('<rect x="62" y="124" width="126" height="92" rx="12" fill="#121315"/>')
    o('<rect x="72" y="133" width="106" height="74" rx="3" fill="#1d221f"/>')
    o('<path d="M72 136 Q72 133 75 133 H140 L96 207 H75 Q72 207 72 204 Z" fill="#ffffff" opacity=".05"/>')
    o('<rect x="66" y="223" width="66" height="12" rx="6" fill="#2b2c2f"/>')
    o('<rect x="152" y="223" width="32" height="12" rx="6" fill="#2b2c2f"/>')
    # Keypad: MENU < > EXIT row, then 1 2 3 * / 4 5 6 0 / 7 8 9 #.
    labels = [['MENU', '<', '>', 'EXIT'], ['1', '2', '3', '*'], ['4', '5', '6', '0'], ['7', '8', '9', '#']]
    for r, row in enumerate(labels):
        y, kh = (243, 20) if r == 0 else (270 + (r - 1) * 23, 17)
        for c, label in enumerate(row):
            x = 64 + c * 31
            o(f'<rect x="{x}" y="{y}" width="27" height="{kh}" rx="4" fill="#141518"/>')
            o(f'<rect x="{x + 4}" y="{y + 1}" width="19" height="1.2" fill="#8a8d92" opacity=".6"/>')
            cx, cy = x + 13.5, y + kh / 2
            if label in '<>':
                d = -1 if label == '<' else 1
                o(f'<path d="M{cx - 2.5 * d} {cy - 5} L{cx + 2.5 * d} {cy} L{cx - 2.5 * d} {cy + 5}" fill="none" '
                  'stroke="#f5f6f7" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>')
            else:
                size = 7 if len(label) > 1 else (16 if label == '*' else 11)
                o(f'<text x="{cx}" y="{cy + (2.5 if label == "*" else 0):g}" {FONT} font-size="{size}" font-weight="700" fill="#f5f6f7" '
                  f'text-anchor="middle" dominant-baseline="central">{label}</text>')
    o('<rect x="53" y="336" width="144" height="5" fill="#8d9096" opacity=".5"/>')
    # USB-C plug in the right rail, cable and USB-A plug.
    o(f'<rect x="206" y="246" width="16" height="12" rx="3" fill="{CONNECT_METAL}"/>')
    o(f'<path d="M222 252 C282 252 270 160 324 160" fill="none" stroke="{CONNECT_BLUE}" stroke-width="8" '
      'stroke-linecap="round"/>')
    o(f'<rect x="320" y="146" width="34" height="28" rx="5" fill="{CONNECT_BLUE}"/>')
    o(f'<rect x="352" y="151" width="30" height="18" rx="2" fill="none" stroke="{CONNECT_METAL}" stroke-width="3.5"/>')
    o(f'<rect x="357" y="156" width="20" height="4" fill="{CONNECT_METAL}"/>')
    o('</svg>')
    return '\n'.join(out) + '\n'


def build_connect_text_svg():
    _, _, w, h = CONNECT_TEXT
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">\n'
            f'<rect width="{w}" height="{h}" fill="#000"/>\n'
            f'<text x="{w / 2:g}" y="52" {FONT} font-size="42" font-weight="600" text-anchor="middle" '
            'fill="#f4f5f6">Connect your UV-K1</text>\n'
            f'<text x="{w / 2:g}" y="104" {FONT} font-size="25" text-anchor="middle" fill="#8b9097">'
            'USB-C on the radio  \u203a  USB-A on the Tab5</text>\n'
            '</svg>\n')


def build_layout_header():
    gx, gy, gw, gh = GLASS
    lx, ly, lw, lh = LCD
    assert lw == 128 * LCD_CELL_W and lh * LCD_ROW_NUM == 64 * LCD_ROW_DEN
    led_x0 = math.floor(min(LED_BOT_X[0], LED_TOP_X[0])) - LED_GLOW
    led_x1 = math.ceil(max(LED_BOT_X[1], LED_TOP_X[1])) + LED_GLOW
    led_y0 = math.floor(LED_TOP_Y) - LED_GLOW
    led_y1 = math.ceil(LED_BOT_Y) + LED_GLOW

    # The lit area holds the halo and the beam, which is widest at the top of the screen.
    torch_half_w = math.ceil(TORCH_RX + (TORCH_Y + DY) * TORCH_BEAM_SPREAD) + 2
    torch_box_x = round(TORCH_X) - torch_half_w
    torch_box_h = math.ceil(TORCH_Y + TORCH_RY + TORCH_HALO) + DY

    keys = []
    for label, code, x, y, w, h in key_rects():
        keys.append(f'    {{{x}, {y + DY}, {w}, {h}, 0x{code:02X}}}, /* {label} */')
    side_keys = []
    for label, code, y, h, hit_y, hit_h in side_key_hits():
        side_keys.append(f'    {{{SIDE_KEY_X}, {y + DY}, {SIDE_KEY_W}, {h}, 0, {hit_y + DY}, {SIDE_HIT_W}, {hit_h}, '
                         f'0x{code:02X}}}, /* {label} */')

    return f'''/*
 * UV-K1 skin geometry for the Tab5 portrait viewer, in screen pixels.
 *
 * Generated by scripts/gen_uv_k1_skin.py with the SVGs in assets/.
 * Do not edit: change the script and regenerate all its outputs.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef UV_K1_SKIN_LAYOUT_H
#define UV_K1_SKIN_LAYOUT_H

#include <stdint.h>

#define SKIN_WIDTH            {W}
#define SKIN_HEIGHT           {H}

/* LCD glass, drawn by the viewer over the skin. */
#define SKIN_GLASS_X          {gx}
#define SKIN_GLASS_Y          {gy + DY}
#define SKIN_GLASS_W          {gw}
#define SKIN_GLASS_H          {gh}

/* 128x64 active area inside the glass: {LCD_CELL_W} px per column, 4.5 px per row. */
#define SKIN_LCD_X            {lx - gx}
#define SKIN_LCD_Y            {ly - gy}
#define SKIN_LCD_W            {lw}
#define SKIN_LCD_H            {lh}
#define SKIN_LCD_CELL_W       {LCD_CELL_W}
#define SKIN_LCD_ROW_NUM      {LCD_ROW_NUM}
#define SKIN_LCD_ROW_DEN      {LCD_ROW_DEN}

/* RX/TX light pipe at the antenna base (trapezoid) and its glow box. */
#define SKIN_LED_TOP_Y        {LED_TOP_Y + DY:.1f}f
#define SKIN_LED_BOTTOM_Y     {LED_BOT_Y + DY:.1f}f
#define SKIN_LED_TOP_X0       {LED_TOP_X[0]:.1f}f
#define SKIN_LED_TOP_X1       {LED_TOP_X[1]:.1f}f
#define SKIN_LED_BOTTOM_X0    {LED_BOT_X[0]:.1f}f
#define SKIN_LED_BOTTOM_X1    {LED_BOT_X[1]:.1f}f
#define SKIN_LED_GLOW         {LED_GLOW:.1f}f
#define SKIN_LED_BOX_X        {led_x0}
#define SKIN_LED_BOX_Y        {led_y0 + DY}
#define SKIN_LED_BOX_W        {led_x1 - led_x0}
#define SKIN_LED_BOX_H        {led_y1 - led_y0}

/* White torch LED on top, lit by the viewer while the radio flashlight is on:
 * dome, halo and a beam fanning out up to the top of the screen. */
#define SKIN_TORCH_X          {TORCH_X:.1f}f
#define SKIN_TORCH_Y          {TORCH_Y + DY:.1f}f
#define SKIN_TORCH_RX         {TORCH_RX:.1f}f
#define SKIN_TORCH_RY         {TORCH_RY:.1f}f
#define SKIN_TORCH_TOP        {TORCH_TOP + DY:.1f}f    /* housing edge hiding the lower dome */
#define SKIN_TORCH_HALO       {TORCH_HALO:.1f}f
#define SKIN_TORCH_BEAM_SPREAD {TORCH_BEAM_SPREAD:.2f}f   /* beam half-width growth per pixel of rise */
#define SKIN_TORCH_BOX_X      {torch_box_x}
#define SKIN_TORCH_BOX_Y      0
#define SKIN_TORCH_BOX_W      {2 * torch_half_w}
#define SKIN_TORCH_BOX_H      {torch_box_h}

/* "Connect the radio" screen, shown on black while no K1 is connected. */
#define SKIN_CONNECT_ICON_X   {CONNECT_ICON[0]}
#define SKIN_CONNECT_ICON_Y   {CONNECT_ICON[1]}
#define SKIN_CONNECT_ICON_W   {CONNECT_ICON[2]}
#define SKIN_CONNECT_ICON_H   {CONNECT_ICON[3]}
#define SKIN_CONNECT_TEXT_X   {CONNECT_TEXT[0]}
#define SKIN_CONNECT_TEXT_Y   {CONNECT_TEXT[1]}
#define SKIN_CONNECT_TEXT_W   {CONNECT_TEXT[2]}
#define SKIN_CONNECT_TEXT_H   {CONNECT_TEXT[3]}

#define SKIN_KEY_RADIUS       {KEY_RADIUS}
#define SKIN_KEY_COUNT        {len(keys)}

typedef struct {{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint8_t code;               /* firmware KEY_* code (App/driver/keyboard.h) */
}} skin_key_t;

static const skin_key_t skin_keys[SKIN_KEY_COUNT] = {{
{chr(10).join(keys)}
}};

/* F1 and F2 on the left rail: the drawn key and its wider touch area. */
#define SKIN_SIDE_KEY_RADIUS  {SIDE_KEY_RADIUS}
#define SKIN_SIDE_KEY_COUNT   {len(side_keys)}

typedef struct {{
    int16_t x;                  /* drawn key */
    int16_t y;
    int16_t w;
    int16_t h;
    int16_t hit_x;              /* touch area */
    int16_t hit_y;
    int16_t hit_w;
    int16_t hit_h;
    uint8_t code;
}} skin_side_key_t;

static const skin_side_key_t skin_side_keys[SKIN_SIDE_KEY_COUNT] = {{
{chr(10).join(side_keys)}
}};

#endif
'''


def find_chrome(explicit):
    for candidate in ([explicit] if explicit else CHROME_CANDIDATES):
        path = shutil.which(candidate) or (candidate if Path(candidate).exists() else None)
        if path:
            return path
    return None


def render_png(chrome, svg_path, size):
    png_path = svg_path.with_suffix('.png')
    subprocess.run([chrome, '--headless=new', '--disable-gpu', '--hide-scrollbars',
                    f'--window-size={size[0]},{size[1]}', f'--screenshot={png_path}', svg_path.as_uri()],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return png_path


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--screenshot', type=Path, default=DEFAULT_SCREENSHOT,
                        help='256x192 firmware screenshot shown on the showcase LCD')
    parser.add_argument('--render', action='store_true',
                        help='also render the SVGs to PNG with headless Chrome')
    parser.add_argument('--chrome', help='Chrome or Chromium executable for --render')
    args = parser.parse_args()

    assets = PROJECT_DIR / 'assets'
    images = {
        assets / 'uv-k1-720x1280.svg': (build_svg(lcd_path(args.screenshot), led_lit=True), (W, H)),
        assets / 'uv-k1-skin.svg': (build_svg(None, led_lit=False), (W, H)),
        assets / 'k1-connect-icon.svg': (build_connect_icon_svg(), CONNECT_ICON[2:]),
        assets / 'k1-connect-text.svg': (build_connect_text_svg(), CONNECT_TEXT[2:]),
    }
    for path, (text, _) in images.items():
        path.write_text(text)
        print(f'wrote {path.relative_to(PROJECT_DIR)}')

    # Everything but the showcase render goes into the firmware.
    embedded = [f'embed_rgb565({svg.stem} {svg.stem.replace("-", "_")}_rgb565 {w} {h})'
                for svg, (_, (w, h)) in images.items() if svg.stem != 'uv-k1-720x1280']
    generated = {
        PROJECT_DIR / 'main' / 'uv_k1_skin_layout.h': build_layout_header(),
        PROJECT_DIR / 'main' / 'uv_k1_skin_images.cmake':
            '# Generated by scripts/gen_uv_k1_skin.py: images embedded by main/CMakeLists.txt.\n'
            '# SPDX-License-Identifier: GPL-3.0-or-later\n'
            + '\n'.join(embedded) + '\n',
    }
    for path, text in generated.items():
        path.write_text(text)
        print(f'wrote {path.relative_to(PROJECT_DIR)}')

    if args.render:
        chrome = find_chrome(args.chrome)
        if not chrome:
            sys.exit('error: Chrome not found, pass --chrome PATH')
        for svg, (_, size) in images.items():
            print(f'rendered {render_png(chrome, svg, size).relative_to(PROJECT_DIR)}')


if __name__ == '__main__':
    main()
