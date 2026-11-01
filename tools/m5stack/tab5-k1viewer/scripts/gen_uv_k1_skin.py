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
  assets/uv-k1-landscape.svg  landscape (1280x720) showcase: large LCD, keypad on the right
  assets/uv-k1-landscape-skin.svg  landscape firmware background
  assets/uv-k1-screen.svg     screen-only (1280x720) showcase: the LCD alone, very large
  assets/uv-k1-screen-skin.svg     screen-only firmware background
  assets/k1-connect-icon.svg  "connect the radio" icon, shown on black when no K1
  assets/k1-connect-text.svg  "connect the radio" message under the icon
  assets/k1-connect-credit.svg  author and license, at the bottom of that screen
  assets/k1-toast-*.svg       toasts after a touch on the plate: orientation lock,
                              key beep, brightness gauge
  main/uv_k1_skin_layout.h    portrait, landscape and screen-only layouts: glass, LCD,
                              LED, torch, keys
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
LED_CENTER_X = (LED_TOP_X[0] + LED_TOP_X[1]) / 2
LED_GLOW = 10
TORCH_X, TORCH_Y = 352.0, 96.0       # white torch LED dome on top, between antenna and knob
TORCH_RX, TORCH_RY = 17.0, 6.5
TORCH_TOP = 94                        # top edge of its housing, which hides the lower part
TORCH_HALO = 24
TORCH_BEAM_SPREAD = 1.0               # beam half-width growth per pixel of rise
CONNECT_ICON = (160, 360, 400, 360)  # screen coordinates of the "connect" icon and message
CONNECT_TEXT = (60, 760, 600, 130)
# Author and license at the bottom of the screen; the viewer writes the
# application version on one more line, 18 px tall, right under it.
CONNECT_CREDIT = (60, 1178, 600, 56)
PLATE = (155, 180, 410, 76)           # QUANSHENG plate, in drawing coordinates
PLATE_HIT = 6                         # touch margin around the plate
TOAST = (256, 150)                    # message shown after a touch on the plate
TOAST_COLOR = '#181c20'               # exact in RGB565, like the panel drawn around it
TOAST_BRIGHTNESS_LEVELS = 4           # backlight levels, brightest first
CONNECT_BLUE = '#3d93e6'

# ---- landscape layout (1280x720): large LCD on the left, keypad on the right ----
LW, LH = 1280, 720
L_BEZEL = (64, 92, 692, 596)          # x, y, w, h
L_GLASS_MARGIN = (12, 10)
L_LCD_CELL_W = 5                      # 128x64 LCD pixels at 5 x 7.5 (real 1:1.5 pixel aspect)
L_LCD_ROW_NUM, L_LCD_ROW_DEN = 2, 15  # LCD row = screen row * 2 / 15
L_LOGO = (410, 18, 0.8)               # centre x, top and scale of the QUANSHENG plate
L_TOP_BAR = (1006, 26)                # centre x and top of the bar with the RX/TX LED and torch
L_PILLS = [('F1', 0x12, 800), ('F2', 0x11, 1016)]   # side keys as pills: label, code, x
L_PILL_W, L_PILL_H, L_PILL_RADIUS = 196, 50, 15
L_PILL_HIT = 8                        # touch margin around the pills
# Portrait plates and keys reused on the right, from x = L_KEYPAD_X. The right
# column is level with the glass of the screen (see landscape_column).
L_KEYPAD_X = 770
L_KEYPAD_SCALE = 0.855
L_PLATES_ORIGIN = (84, 694)           # portrait point placed at (L_KEYPAD_X, plates y)
L_KEYS_ORIGIN = (84, 782)             # portrait point placed at (L_KEYPAD_X, keys y)
L_TORCH_RX, L_TORCH_RY = 20.0, 8.0    # torch seen from above: the whole lens shows
L_CONNECT_ICON = (440, 84)
L_CONNECT_TEXT = (340, 474)
L_CONNECT_CREDIT = (340, 630)

# ---- screen-only layout (1280x720, USB-A port on the left): the LCD alone ----
# The screen fills nearly the full height; the left band holds the torch, the
# QUANSHENG plate and the RX/TX light pipe, upright. Grip, plate, bezel and grip
# are spaced by equal gaps. Corners are concentric from the inside out: square
# glass (the LCD canvas covers it), rounded black rim, then the bezel.
S_GLASS_MARGIN = (10, 4)
S_LCD_CELL_W = 7                      # 128x64 LCD pixels at 7 x 10.5 (real 1:1.5 pixel aspect)
S_LCD_ROW_NUM, S_LCD_ROW_DEN = 2, 21
S_RIM, S_RIM_RX = 6, 10               # black rim around the glass, its corner radius
S_BORDER = 8                          # bezel around the rim (bezel radius = rim radius + border)
S_GRIPS = (56, LW - 56)               # inner edges of the black grips
S_PLATE_W = 77.5                      # upright plate width, stroke included
_s_bezel_w = 128 * S_LCD_CELL_W + 2 * (S_GLASS_MARGIN[0] + S_RIM + S_BORDER)
_s_bezel_h = 64 * S_LCD_ROW_DEN // S_LCD_ROW_NUM + 2 * (S_GLASS_MARGIN[1] + S_RIM + S_BORDER)
_s_gap = (S_GRIPS[1] - S_GRIPS[0] - S_PLATE_W - _s_bezel_w) / 3
S_BEZEL = (round(S_GRIPS[1] - _s_gap - _s_bezel_w), (LH - _s_bezel_h) // 2, _s_bezel_w, _s_bezel_h)
S_BAND_X = round((S_GRIPS[0] + S_BEZEL[0]) / 2)  # centre of the left band
S_PLATE = (S_BAND_X, 360, 1.0)        # centre and scale of the upright plate
S_TORCH_POD = (S_BAND_X, 70)          # upright pods above and below the plate
S_LED_POD = (S_BAND_X, 650)
S_POD_W, S_POD_H = 56, 120
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
    px, py, pw, ph = PLATE
    o(f'<rect x="{px}" y="{py}" width="{pw}" height="{ph}" rx="6" fill="#0b0b0c" stroke="#3a3c40" stroke-width="1.5"/>')
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
    o('<!-- plates -->')
    for x, w, label in ((129, 254, 'Mini Kong'), (482, 114, 'MIC')):
        o(f'<rect x="{x - 3}" y="{PLATE_Y - 3}" width="{w + 6}" height="55" rx="16" fill="#18191b"/>')
        o(f'<rect x="{x}" y="{PLATE_Y}" width="{w}" height="49" rx="14" fill="url(#plate)"/>')
        o(f'<rect x="{x + 8}" y="{PLATE_Y + 1.5}" width="{w - 16}" height="2" rx="1" fill="#6a6d72" opacity=".6"/>')
        cx = x + w / 2
        o(f'<text x="{cx}" y="{PLATE_Y + 34}" {FONT} font-size="25" font-weight="700" text-anchor="middle" '
          f'fill="#55585d">{label}</text>')
        o(f'<text x="{cx}" y="{PLATE_Y + 32.5}" {FONT} font-size="25" font-weight="700" text-anchor="middle" '
          f'fill="#1b1c1e">{label}</text>')
    o('<!-- /plates -->')

    # ---- microphone hole ----
    o('<!-- keypad -->')
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
    o('<!-- /keypad -->')

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


def build_connect_credit_svg():
    _, _, w, h = CONNECT_CREDIT
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">\n'
            f'<rect width="{w}" height="{h}" fill="#000"/>\n'
            f'<text x="{w / 2:g}" y="21" {FONT} font-size="19" font-weight="600" text-anchor="middle" '
            'fill="#7d828a">K1Viewer \u00b7 \u00a9 2026 Armel F4HWN</text>\n'
            f'<text x="{w / 2:g}" y="47" {FONT} font-size="16" text-anchor="middle" fill="#5a5f66">'
            'Free software under the GNU GPL v3 or later</text>\n'
            '</svg>\n')


def build_toast_svg(icon, text, text_y=128):
    """Toast shown after a touch on the plate: an icon above a short message,
    on the flat colour of the panel the viewer draws around it."""
    w, h = TOAST
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">\n'
            f'<rect width="{w}" height="{h}" fill="{TOAST_COLOR}"/>\n'
            + icon
            + f'<text x="{w / 2:g}" y="{text_y}" {FONT} font-size="22" font-weight="600" text-anchor="middle" '
            f'fill="#e6e8eb">{text}</text>\n'
            '</svg>\n')


def toast_lock_svg(locked):
    """Padlock, closed or with its shackle lifted out of the body."""
    shackle = 'M114 46 V32 A14 14 0 0 1 142 32 V46' if locked else 'M142 46 V24 A14 14 0 0 0 114 24 V30'
    icon = (f'<path d="{shackle}" fill="none" stroke="#c9cdd3" stroke-width="7" stroke-linecap="round"/>\n'
            f'<rect x="102" y="44" width="52" height="40" rx="8" fill="{CONNECT_BLUE if locked else "#c9cdd3"}"/>\n'
            f'<circle cx="128" cy="60" r="5" fill="{TOAST_COLOR}"/>\n'
            f'<rect x="126" y="62" width="4" height="10" rx="2" fill="{TOAST_COLOR}"/>\n')
    return build_toast_svg(icon, 'Orientation locked' if locked else 'Orientation unlocked', 122)


def toast_beep_svg(on):
    """Loudspeaker with sound waves, or crossed out."""
    icon = (f'<path d="M104 38 H116 L132 24 V72 L116 58 H104 Z" fill="#c9cdd3" stroke="#c9cdd3" '
            'stroke-width="2" stroke-linejoin="round"/>\n')
    if on:
        waves = ''.join(f'<path d="M{132 + r * math.cos(-0.7):.1f} {48 + r * math.sin(-0.7):.1f} '
                        f'A{r} {r} 0 0 1 {132 + r * math.cos(0.7):.1f} {48 + r * math.sin(0.7):.1f}"/>'
                        for r in (11, 21))
        icon += (f'<g fill="none" stroke="{CONNECT_BLUE}" stroke-width="5" stroke-linecap="round">'
                 f'{waves}</g>\n')
    else:
        icon += ('<g stroke="#8b9097" stroke-width="5" stroke-linecap="round">'
                 '<line x1="142" y1="39" x2="158" y2="57"/><line x1="158" y1="39" x2="142" y2="57"/></g>\n')
    return build_toast_svg(icon, 'Key beep on' if on else 'Key beep off', 122)


def toast_brightness_svg(level):
    """Sun and a gauge with one bar per backlight level; level 0 is the brightest."""
    rays = ''.join(f'<line x1="{128 + 17 * math.cos(a):.1f}" y1="{48 + 17 * math.sin(a):.1f}" '
                   f'x2="{128 + 25 * math.cos(a):.1f}" y2="{48 + 25 * math.sin(a):.1f}"/>'
                   for a in (i * math.pi / 4 for i in range(8)))
    lit = TOAST_BRIGHTNESS_LEVELS - level
    x0 = (TOAST[0] - (TOAST_BRIGHTNESS_LEVELS * 44 - 8)) // 2
    bars = ''.join(f'<rect x="{x0 + i * 44}" y="88" width="36" height="8" rx="4" '
                   f'fill="{CONNECT_BLUE if i < lit else "#3a3f46"}"/>'
                   for i in range(TOAST_BRIGHTNESS_LEVELS))
    icon = (f'<circle cx="128" cy="48" r="11" fill="#f2c94c"/>\n'
            f'<g stroke="#f2c94c" stroke-width="4" stroke-linecap="round">{rays}</g>\n'
            f'{bars}\n')
    return build_toast_svg(icon, 'Brightness', 132)


def led_geometry(dx, dy, vertical=False):
    """RX/TX light pipe moved by (dx, dy) from its portrait drawing, with its glow
    box. A vertical pipe is turned a quarter turn counter-clockwise about its centre."""
    top_x = (LED_TOP_X[0] + dx, LED_TOP_X[1] + dx)
    bot_x = (LED_BOT_X[0] + dx, LED_BOT_X[1] + dx)
    top_y, bot_y = LED_TOP_Y + dy, LED_BOT_Y + dy
    half_w = (max(bot_x[1], top_x[1]) - min(bot_x[0], top_x[0])) / 2 + LED_GLOW
    half_h = (bot_y - top_y) / 2 + LED_GLOW
    if vertical:
        half_w, half_h = half_h, half_w
    cx = (top_x[0] + top_x[1] + bot_x[0] + bot_x[1]) / 4 if vertical else (min(bot_x[0], top_x[0]) + max(bot_x[1], top_x[1])) / 2
    cy = (top_y + bot_y) / 2
    x0, x1 = math.floor(cx - half_w), math.ceil(cx + half_w)
    y0, y1 = math.floor(cy - half_h), math.ceil(cy + half_h)
    return dict(top_y=top_y, bottom_y=bot_y, top_x0=top_x[0], top_x1=top_x[1],
                bottom_x0=bot_x[0], bottom_x1=bot_x[1], glow=LED_GLOW, vertical=vertical,
                box=(x0, y0, x1 - x0, y1 - y0))


def torch_geometry(x, y, rx, ry, top, beam=1.0):
    """Torch lens and the area lit by its halo and, when beam is not 0, by its
    beam fanning out up to the top of the screen."""
    if beam:
        half_w = math.ceil(rx + y * TORCH_BEAM_SPREAD) + 2
        box = (round(x) - half_w, 0, 2 * half_w, math.ceil(y + ry + TORCH_HALO))
    else:
        x0, y0 = math.floor(x - rx - TORCH_HALO), math.floor(y - ry - TORCH_HALO)
        box = (x0, y0, math.ceil(x + rx + TORCH_HALO) - x0, math.ceil(y + ry + TORCH_HALO) - y0)
    return dict(x=x, y=y, rx=rx, ry=ry, top=top, halo=TORCH_HALO, beam_spread=TORCH_BEAM_SPREAD,
                beam=beam, box=box)


def bezel_glass(bezel, cell_w, row_num, row_den, margin):
    """Glass rectangle centred in a bezel, and the LCD active area relative to it."""
    bx, by, bw, bh = bezel
    mx, my = margin
    lw, lh = 128 * cell_w, 64 * row_den // row_num
    gx, gy = bx + (bw - lw) // 2 - mx, by + (bh - lh) // 2 - my
    return (gx, gy, lw + 2 * mx, lh + 2 * my), (mx, my, lw, lh)


def landscape_column():
    """Landscape right column, level with the glass of the screen: the dark rim
    of the F1/F2 pills at its top, the well of the last key row at its bottom,
    the plates halfway between. Returns the screen y of the pills, plates and keys."""
    s = L_KEYPAD_SCALE
    (_, gy, _, gh), _ = bezel_glass(L_BEZEL, L_LCD_CELL_W, L_LCD_ROW_NUM, L_LCD_ROW_DEN, L_GLASS_MARGIN)
    rim, well = 3, 5                  # dark rim of the pills, dark well around each key
    pill_y = gy + rim
    last_row_bottom = NUM_Y0 + 2 * NUM_PITCH + NUM_H + well + DY
    keys_y = gy + gh - (last_row_bottom - L_KEYS_ORIGIN[1]) * s
    menu_top = keys_y + (ROW1_Y - well + DY - L_KEYS_ORIGIN[1]) * s
    pills_bottom = pill_y + L_PILL_H + rim
    plates_top, plates_h = PLATE_Y - 3 + DY, 55
    plates_y = ((pills_bottom + menu_top - plates_h * s) / 2
                - (plates_top - L_PLATES_ORIGIN[1]) * s)
    return pill_y, plates_y, keys_y


def plate_hit(x, y, w, h):
    """Touch area of the QUANSHENG plate drawn at (x, y, w, h), with its margin."""
    x0, y0 = math.floor(x - PLATE_HIT), math.floor(y - PLATE_HIT)
    return (x0, y0, math.ceil(x + w + PLATE_HIT) - x0, math.ceil(y + h + PLATE_HIT) - y0)


def layout_data():
    """Screen geometry of the portrait, landscape and screen-only layouts, for the C header."""
    keys = [(label, code, x, y + DY, w, h) for label, code, x, y, w, h in key_rects()]
    px, py, pw, ph = PLATE
    portrait = dict(
        name='portrait', size=(W, H),
        glass=(GLASS[0], GLASS[1] + DY, GLASS[2], GLASS[3]),
        lcd=(LCD[0] - GLASS[0], LCD[1] - GLASS[1], LCD[2], LCD[3]),
        cell_w=LCD_CELL_W, row=(LCD_ROW_NUM, LCD_ROW_DEN),
        key_radius=KEY_RADIUS, keys=keys,
        side_keys=[(label, code, (SIDE_KEY_X, y + DY, SIDE_KEY_W, h), (0, hit_y + DY, SIDE_HIT_W, hit_h),
                    SIDE_KEY_RADIUS) for label, code, y, h, hit_y, hit_h in side_key_hits()],
        led=led_geometry(0, DY),
        torch=torch_geometry(TORCH_X, TORCH_Y + DY, TORCH_RX, TORCH_RY, TORCH_TOP + DY),
        plate=plate_hit(px, py + DY, pw, ph),
        connect_icon=CONNECT_ICON[:2], connect_text=CONNECT_TEXT[:2], connect_credit=CONNECT_CREDIT[:2])

    glass, lcd = bezel_glass(L_BEZEL, L_LCD_CELL_W, L_LCD_ROW_NUM, L_LCD_ROW_DEN, L_GLASS_MARGIN)
    vx, vy = L_KEYS_ORIGIN
    pill_y, _, ky = landscape_column()
    kx = L_KEYPAD_X
    s = L_KEYPAD_SCALE
    cx, top = L_TOP_BAR
    m = L_PILL_HIT
    # The plate is placed by landscape_frame: its point (360, 180) at (lcx, ltop).
    lcx, ltop, ls = L_LOGO
    landscape = dict(
        name='landscape', size=(LW, LH), glass=glass, lcd=lcd,
        cell_w=L_LCD_CELL_W, row=(L_LCD_ROW_NUM, L_LCD_ROW_DEN),
        key_radius=round(KEY_RADIUS * s),
        keys=[(label, code, round(kx + (x - vx) * s), round(ky + (y - vy) * s), round(w * s), round(h * s))
              for label, code, x, y, w, h in keys],
        side_keys=[(label, code, (x, pill_y, L_PILL_W, L_PILL_H),
                    (x - m, pill_y - m, L_PILL_W + 2 * m, L_PILL_H + 2 * m), L_PILL_RADIUS)
                   for label, code, x in L_PILLS],
        led=led_geometry(cx - 75 - LED_CENTER_X, top + 12 - LED_TOP_Y),
        torch=torch_geometry(cx + 75, top + 23, L_TORCH_RX, L_TORCH_RY, LH),
        plate=plate_hit(lcx + (px - 360) * ls, ltop + (py - 180) * ls, pw * ls, ph * ls),
        connect_icon=L_CONNECT_ICON, connect_text=L_CONNECT_TEXT, connect_credit=L_CONNECT_CREDIT)

    glass, lcd = bezel_glass(S_BEZEL, S_LCD_CELL_W, S_LCD_ROW_NUM, S_LCD_ROW_DEN, S_GLASS_MARGIN)
    (led_x, led_y), (torch_x, torch_y) = S_LED_POD, S_TORCH_POD
    # The plate is turned a quarter turn counter-clockwise about its point
    # (360, 218), placed at the centre of the band (see build_screen_svg).
    scx, scy, ss = S_PLATE
    screen = dict(
        name='screen', size=(LW, LH), glass=glass, lcd=lcd,
        cell_w=S_LCD_CELL_W, row=(S_LCD_ROW_NUM, S_LCD_ROW_DEN),
        key_radius=0, keys=[], side_keys=[],
        led=led_geometry(led_x - LED_CENTER_X, led_y - (LED_TOP_Y + LED_BOT_Y) / 2, vertical=True),
        torch=torch_geometry(torch_x, torch_y, L_TORCH_RY, L_TORCH_RX, LH, beam=0.0),
        plate=plate_hit(scx + (py - 218) * ss, scy - (px + pw - 360) * ss, ph * ss, pw * ss),
        connect_icon=L_CONNECT_ICON, connect_text=L_CONNECT_TEXT, connect_credit=L_CONNECT_CREDIT)
    return portrait, landscape, screen


LANDSCAPE_DEFS = '''<defs>
  <linearGradient id="pill" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#3a3c41"/><stop offset=".2" stop-color="#232427"/>
    <stop offset="1" stop-color="#121315"/>
  </linearGradient>
  <linearGradient id="bodyV" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#4b4e53"/><stop offset=".07" stop-color="#73767c"/>
    <stop offset=".5" stop-color="#6c6f75"/><stop offset=".93" stop-color="#5d6065"/>
    <stop offset="1" stop-color="#383b3f"/>
  </linearGradient>
</defs>'''


def landscape_frame(o, out, portrait, logo):
    """Horizontal body with black grips at both ends, and the QUANSHENG plate."""
    o(f'<rect x="8" y="8" width="{LW - 16}" height="{LH - 16}" rx="44" fill="url(#bodyV)"/>')
    o(f'<path d="M8 52 Q8 8 52 8 H56 V{LH - 8} H52 Q8 {LH - 8} 8 {LH - 52} Z" fill="url(#railL)"/>')
    o(f'<path d="M{LW - 8} 52 Q{LW - 8} 8 {LW - 52} 8 H{LW - 56} V{LH - 8} H{LW - 52} Q{LW - 8} {LH - 8} '
      f'{LW - 8} {LH - 52} Z" fill="url(#railR)"/>')
    # The plate is borrowed from the portrait drawing.
    lines = portrait.split('\n')
    first = next(i for i, line in enumerate(lines) if line.startswith('<rect x="155" y="180"'))
    last = next(i for i, line in enumerate(lines) if 'SHENG</tspan></text>' in line)
    lcx, ltop, lscale = logo
    o(f'<g transform="translate({lcx - 360 * lscale:g} {ltop - 180 * lscale:g}) scale({lscale:g})">')
    out.extend(lines[first:last + 1])
    o('</g>')


def bezel_svg(o, bezel, glass, lcd_rect, lcd, rx=40, frame=True, rim=6, rim_rx=7, glass_rx=4):
    """Screen bezel, black rim and glass; the LCD pixels when a screenshot is given."""
    bx, by, bw, bh = bezel
    gx, gy, gw, gh = glass
    lx, ly, lw, lh = lcd_rect
    o(f'<rect x="{bx - 2}" y="{by + 3}" width="{bw + 4}" height="{bh + 4}" rx="{rx + 2}" fill="#000" opacity=".35"/>')
    o(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="{rx}" fill="url(#bezel)"/>')
    if frame:
        o(f'<rect x="{bx + 12}" y="{by + 12}" width="{bw - 24}" height="{bh - 24}" rx="30" fill="none" '
          'stroke="#2c2e31" stroke-width="2"/>')
    o(f'<rect x="{gx - rim}" y="{gy - rim}" width="{gw + 2 * rim}" height="{gh + 2 * rim}" rx="{rim_rx}" '
      'fill="#050506"/>')
    o(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="{glass_rx}" fill="url(#glass)"/>')
    if lcd:
        sx, sy = lw / 128, lh / 64
        o(f'<path transform="translate({gx + lx + 1.5} {gy + ly + 3}) scale({sx:g} {sy:g})" d="{lcd}" '
          'fill="#000" opacity=".13"/>')
        o(f'<path transform="translate({gx + lx} {gy + ly}) scale({sx:g} {sy:g})" d="{lcd}" fill="#17191b"/>')
        o(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="{glass_rx}" fill="url(#gloss)"/>')


def led_svg(o, led, lit):
    """RX/TX light pipe, lit green for the showcase or off for the firmware skin."""
    d = (f'M{led["top_x0"]:g} {led["top_y"]:g} H{led["top_x1"]:g} L{led["bottom_x1"]:g} {led["bottom_y"]:g} '
         f'H{led["bottom_x0"]:g} Z')
    if led.get('vertical'):
        cx = (led['top_x0'] + led['top_x1'] + led['bottom_x0'] + led['bottom_x1']) / 4
        cy = (led['top_y'] + led['bottom_y']) / 2
        o(f'<g transform="rotate(-90 {cx:g} {cy:g})">')
    if lit:
        o(f'<path d="{d}" fill="#35ff6b" opacity=".75" filter="url(#glow)"/>')
        o(f'<path d="{d}" fill="url(#ledGreen)" stroke="#0f6b2a" stroke-width="1.2"/>')
    else:
        o(f'<path d="{d}" fill="#18231b" stroke="#0a120c" stroke-width="1.2"/>')
    if led.get('vertical'):
        o('</g>')


def torch_svg(o, torch):
    """Torch seen from above: housing and the whole lens, lying or upright."""
    x, y = torch['x'], torch['y']
    if torch['ry'] > torch['rx']:
        o(f'<rect x="{x - 14:g}" y="{y - 30:g}" width="28" height="60" rx="10" fill="url(#cyl)"/>')
    else:
        o(f'<rect x="{x - 30:g}" y="{y - 14:g}" width="60" height="28" rx="10" fill="url(#cyl)"/>')
    o(f'<ellipse cx="{x:g}" cy="{y:g}" rx="{torch["rx"]:g}" ry="{torch["ry"]:g}" '
      'fill="url(#torchLens)" stroke="#6d747c" stroke-width="1.2"/>')
    if torch['ry'] > torch['rx']:
        o(f'<ellipse cx="{x - 3:g}" cy="{y - 5:g}" rx="1.8" ry="6" fill="#ffffff"/>')
    else:
        o(f'<ellipse cx="{x - 5:g}" cy="{y - 3:g}" rx="6" ry="1.8" fill="#ffffff"/>')


def landscape_svg_start():
    return [f'<svg xmlns="http://www.w3.org/2000/svg" width="{LW}" height="{LH}" viewBox="0 0 {LW} {LH}">',
            '<title>Quansheng UV-K1</title>', DEFS, LANDSCAPE_DEFS,
            f'<rect width="{LW}" height="{LH}" fill="url(#bg)"/>']


def build_landscape_svg(lcd, led_lit):
    """Landscape front: large LCD in its bezel, keypad of the portrait drawing on the right."""
    portrait = build_svg(None, led_lit=False)
    data = layout_data()[1]
    out = landscape_svg_start()
    o = out.append

    landscape_frame(o, out, portrait, L_LOGO)
    bezel_svg(o, L_BEZEL, data['glass'], data['lcd'], lcd)

    # ---- top bar: RX/TX light pipe and torch, as on top of the radio ----
    cx, top = L_TOP_BAR
    o(f'<rect x="{cx - 150}" y="{top}" width="300" height="46" rx="16" fill="url(#shoulder)"/>')
    led_svg(o, data['led'], led_lit)
    torch_svg(o, data['torch'])

    # ---- F1 / F2 pills ----
    for label, _, (x, y, w, h), _, radius in data['side_keys']:
        o(f'<rect x="{x - 3}" y="{y - 3}" width="{w + 6}" height="{h + 6}" rx="{radius + 2}" fill="#0c0d0e"/>')
        o(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="url(#pill)" stroke="#050506"/>')
        o(f'<rect x="{x + 10}" y="{y + 2}" width="{w - 20}" height="2" rx="1" fill="#8a8d92" opacity=".55"/>')
        o(f'<text x="{x + w / 2:g}" y="{y + 34}" {FONT} font-size="26" font-weight="700" text-anchor="middle" '
          f'fill="#e6e8eb">{label}</text>')

    # ---- plates and keys of the portrait drawing, scaled, without its background ----
    s = L_KEYPAD_SCALE
    for name, (vx, vy), y in zip(('plates', 'keypad'), (L_PLATES_ORIGIN, L_KEYS_ORIGIN), landscape_column()[1:]):
        part = portrait[portrait.index(f'<!-- {name} -->'):portrait.index(f'<!-- /{name} -->')]
        # The portrait drawing is in local coordinates, shifted down by DY.
        o(f'<g transform="translate({L_KEYPAD_X - vx * s:g} {y + (DY - vy) * s:g}) scale({s:g})">{part}</g>')
    o('</svg>')
    return '\n'.join(out) + '\n'


def build_screen_svg(lcd, led_lit):
    """Screen-only front: the LCD on nearly the full height, the torch, the
    QUANSHENG plate and the RX/TX light pipe upright in the left band."""
    data = layout_data()[2]
    portrait = build_svg(None, led_lit=False)
    out = landscape_svg_start()
    o = out.append

    # Body bleeding past the top and bottom edges, black grips at both ends.
    o(f'<rect x="8" y="-40" width="{LW - 16}" height="{LH + 80}" rx="44" fill="url(#bodyV)"/>')
    o(f'<rect x="8" y="-40" width="48" height="{LH + 80}" rx="20" fill="url(#railL)"/>')
    o(f'<rect x="{LW - 56}" y="-40" width="48" height="{LH + 80}" rx="20" fill="url(#railR)"/>')

    # Left band: upright pods for the light pipe and the torch, upright plate.
    for cx, cy in (S_LED_POD, S_TORCH_POD):
        o(f'<rect x="{cx - S_POD_W / 2:g}" y="{cy - S_POD_H / 2:g}" width="{S_POD_W}" height="{S_POD_H}" '
          'rx="18" fill="url(#shoulder)"/>')
    led_svg(o, data['led'], led_lit)
    torch_svg(o, data['torch'])
    lines = portrait.split('\n')
    first = next(i for i, line in enumerate(lines) if line.startswith('<rect x="155" y="180"'))
    last = next(i for i, line in enumerate(lines) if 'SHENG</tspan></text>' in line)
    px, py, scale = S_PLATE
    o(f'<g transform="translate({px:g} {py:g}) rotate(-90) scale({scale:g}) translate(-360 -218)">')
    out.extend(lines[first:last + 1])
    o('</g>')

    bezel_svg(o, S_BEZEL, data['glass'], data['lcd'], lcd, rx=S_RIM_RX + S_BORDER, frame=False,
              rim=S_RIM, rim_rx=S_RIM_RX, glass_rx=0)
    o('</svg>')
    return '\n'.join(out) + '\n'


def c_rect(rect):
    return '{%d, %d, %d, %d}' % tuple(rect)


def c_layout(d):
    """C arrays and skin_layout_t object of one layout."""
    name = d['name']
    keys = '\n'.join(f'    {{{c_rect((x, y, w, h))}, 0x{code:02X}}}, /* {label} */'
                     for label, code, x, y, w, h in d['keys'])
    side_keys = '\n'.join(f'    {{{c_rect(key)}, {c_rect(hit)}, {radius}, 0x{code:02X}}}, /* {label} */'
                          for label, code, key, hit, radius in d['side_keys'])
    led, torch = d['led'], d['torch']
    lines = ['']
    if keys:
        lines += [f'static const skin_key_t skin_{name}_keys[] = {{', keys, '};', '']
    if side_keys:
        lines += [f'static const skin_side_key_t skin_{name}_side_keys[] = {{', side_keys, '};', '']
    lines += [
        f'static const skin_layout_t skin_{name} = {{',
        f'    .width = {d["size"][0]},',
        f'    .height = {d["size"][1]},',
        f'    .glass = {c_rect(d["glass"])},',
        f'    .lcd = {c_rect(d["lcd"])},',
        f'    .lcd_cell_w = {d["cell_w"]},',
        f'    .lcd_row_num = {d["row"][0]},',
        f'    .lcd_row_den = {d["row"][1]},',
        f'    .key_radius = {d["key_radius"]},',
        *([f'    .keys = skin_{name}_keys,',
           f'    .key_count = sizeof(skin_{name}_keys) / sizeof(skin_{name}_keys[0]),']
          if keys else ['    .keys = NULL,', '    .key_count = 0,']),
        *([f'    .side_keys = skin_{name}_side_keys,',
           f'    .side_key_count = sizeof(skin_{name}_side_keys) / sizeof(skin_{name}_side_keys[0]),']
          if side_keys else ['    .side_keys = NULL,', '    .side_key_count = 0,']),
        '    .led = {',
        *[f'        .{field} = {led[field]:.1f}f,'
          for field in ('top_y', 'bottom_y', 'top_x0', 'top_x1', 'bottom_x0', 'bottom_x1', 'glow')],
        f'        .vertical = {int(led["vertical"])},',
        f'        .box = {c_rect(led["box"])},',
        '    },',
        '    .torch = {',
        *[f'        .{field} = {torch[field]:.2f}f,'
          for field in ('x', 'y', 'rx', 'ry', 'top', 'halo', 'beam_spread', 'beam')],
        f'        .box = {c_rect(torch["box"])},',
        '    },',
        f'    .plate = {c_rect(d["plate"])},',
        f'    .connect_icon_x = {d["connect_icon"][0]},',
        f'    .connect_icon_y = {d["connect_icon"][1]},',
        f'    .connect_text_x = {d["connect_text"][0]},',
        f'    .connect_text_y = {d["connect_text"][1]},',
        f'    .connect_credit_x = {d["connect_credit"][0]},',
        f'    .connect_credit_y = {d["connect_credit"][1]},',
        '};',
    ]
    return '\n'.join(lines) + '\n'


HEADER_TYPES = '''
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} skin_rect_t;

typedef struct {
    skin_rect_t key;
    uint8_t code;               /* firmware KEY_* code (App/driver/keyboard.h) */
} skin_key_t;

/* F1 / F2: the drawn key and a touch area that can be much wider. */
typedef struct {
    skin_rect_t key;
    skin_rect_t hit;
    int16_t radius;
    uint8_t code;
} skin_side_key_t;

/* RX/TX light pipe (trapezoid) and the box holding its glow. */
typedef struct {
    float top_y;
    float bottom_y;
    float top_x0;
    float top_x1;
    float bottom_x0;
    float bottom_x1;
    float glow;
    uint8_t vertical;           /* turned a quarter turn counter-clockwise about its centre */
    skin_rect_t box;
} skin_led_t;

/* White torch lens, and the box lit by its halo and its beam. */
typedef struct {
    float x;
    float y;
    float rx;
    float ry;
    float top;                  /* edge hiding the lower part of the lens */
    float halo;
    float beam_spread;          /* beam half-width growth per pixel of rise */
    float beam;                 /* beam strength, 0 for none */
    skin_rect_t box;
} skin_torch_t;

typedef struct {
    int16_t width;
    int16_t height;
    skin_rect_t glass;          /* drawn by the viewer over the skin */
    skin_rect_t lcd;            /* 128x64 active area, relative to the glass */
    uint8_t lcd_cell_w;         /* screen pixels per LCD column */
    uint8_t lcd_row_num;        /* LCD row = screen row * num / den */
    uint8_t lcd_row_den;
    int16_t key_radius;
    const skin_key_t *keys;
    uint8_t key_count;
    const skin_side_key_t *side_keys;
    uint8_t side_key_count;
    skin_led_t led;
    skin_torch_t torch;
    skin_rect_t plate;          /* touch area of the QUANSHENG plate */
    int16_t connect_icon_x;
    int16_t connect_icon_y;
    int16_t connect_text_x;
    int16_t connect_text_y;
    int16_t connect_credit_x;
    int16_t connect_credit_y;
} skin_layout_t;
'''


def build_layout_header():
    layouts = layout_data()
    for d in layouts:
        lw, lh = d['lcd'][2:]
        num, den = d['row']
        assert lw == 128 * d['cell_w'] and lh * num == 64 * den, d['name']

    head = [
        '/*',
        ' * UV-K1 skin layouts for the Tab5 viewer, in screen pixels: portrait',
        ' * (720x1280), landscape and screen only (1280x720).',
        ' *',
        ' * Generated by scripts/gen_uv_k1_skin.py with the SVGs in assets/.',
        ' * Do not edit: change the script and regenerate all its outputs.',
        ' *',
        ' * SPDX-License-Identifier: GPL-3.0-or-later',
        ' */',
        '',
        '#ifndef UV_K1_SKIN_LAYOUT_H',
        '#define UV_K1_SKIN_LAYOUT_H',
        '',
        '#include <stddef.h>',
        '#include <stdint.h>',
        '',
        '/* "Connect your UV-K1" icon and message, shown on black while no K1 is connected,',
        ' * with the author and license at the bottom. */',
        f'#define SKIN_CONNECT_ICON_W   {CONNECT_ICON[2]}',
        f'#define SKIN_CONNECT_ICON_H   {CONNECT_ICON[3]}',
        f'#define SKIN_CONNECT_TEXT_W   {CONNECT_TEXT[2]}',
        f'#define SKIN_CONNECT_TEXT_H   {CONNECT_TEXT[3]}',
        f'#define SKIN_CONNECT_CREDIT_W {CONNECT_CREDIT[2]}',
        f'#define SKIN_CONNECT_CREDIT_H {CONNECT_CREDIT[3]}',
        '',
        '/* Toasts shown after a touch on the plate (orientation lock, key beep,',
        ' * brightness gauge with one image per backlight level), on a flat colour',
        ' * exact in RGB565. */',
        f'#define SKIN_TOAST_W          {TOAST[0]}',
        f'#define SKIN_TOAST_H          {TOAST[1]}',
        f'#define SKIN_TOAST_COLOR      0x{TOAST_COLOR[1:].upper()}u',
        f'#define SKIN_TOAST_BRIGHTNESS_LEVELS {TOAST_BRIGHTNESS_LEVELS}',
    ]
    return '\n'.join(head) + '\n' + HEADER_TYPES + ''.join(c_layout(d) for d in layouts) + '\n#endif\n'


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
    lcd = lcd_path(args.screenshot)
    showcases = ('uv-k1-720x1280', 'uv-k1-landscape', 'uv-k1-screen')
    images = {
        assets / 'uv-k1-720x1280.svg': (build_svg(lcd, led_lit=True), (W, H)),
        assets / 'uv-k1-skin.svg': (build_svg(None, led_lit=False), (W, H)),
        assets / 'uv-k1-landscape.svg': (build_landscape_svg(lcd, led_lit=True), (LW, LH)),
        assets / 'uv-k1-landscape-skin.svg': (build_landscape_svg(None, led_lit=False), (LW, LH)),
        assets / 'uv-k1-screen.svg': (build_screen_svg(lcd, led_lit=True), (LW, LH)),
        assets / 'uv-k1-screen-skin.svg': (build_screen_svg(None, led_lit=False), (LW, LH)),
        assets / 'k1-connect-icon.svg': (build_connect_icon_svg(), CONNECT_ICON[2:]),
        assets / 'k1-connect-text.svg': (build_connect_text_svg(), CONNECT_TEXT[2:]),
        assets / 'k1-connect-credit.svg': (build_connect_credit_svg(), CONNECT_CREDIT[2:]),
        assets / 'k1-toast-locked.svg': (toast_lock_svg(True), TOAST),
        assets / 'k1-toast-unlocked.svg': (toast_lock_svg(False), TOAST),
        assets / 'k1-toast-beep-on.svg': (toast_beep_svg(True), TOAST),
        assets / 'k1-toast-beep-off.svg': (toast_beep_svg(False), TOAST),
        **{assets / f'k1-toast-brightness-{level}.svg': (toast_brightness_svg(level), TOAST)
           for level in range(TOAST_BRIGHTNESS_LEVELS)},
    }
    for path, (text, _) in images.items():
        path.write_text(text)
        print(f'wrote {path.relative_to(PROJECT_DIR)}')

    # Everything but the showcase renders goes into the firmware.
    embedded = [f'embed_rgb565({svg.stem} {svg.stem.replace("-", "_")}_rgb565 {w} {h})'
                for svg, (_, (w, h)) in images.items() if svg.stem not in showcases]
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
