# QR Map: a position as a QR code, for a phone

QR Map shows a position that another app hands over as a QR code on the
radio's screen. A phone scans it and opens the position on a map, so a
decoded beacon or sonde position goes to the phone without typing a digit.
EPIRB 406 sends it with key 7; any app can do the same (see
[Hand-off message](#hand-off-message)).

Needs a Labs or Fusion_ firmware v6.3.0 or later (app API level 3:
`handoff` / `handoff_read`).

## Using it

1. In EPIRB 406, select a decoded message with a position, press 7, then MENU
   to confirm.
2. QR Map opens with the code on the left and, on the right, the format, the
   latitude and longitude it holds, and the label sent with it (the beacon ID).
3. UP / DOWN (UV-K1: LEFT / RIGHT) change the format, or 1 to 5 choose one
   directly. The choice is saved on exit; the first time it is GOOGLE.
4. Scan the code with the phone's camera or a QR code reader.
5. EXIT goes back to the app that sent the position. A long EXIT goes to the
   main screen.

The backlight stays on while the code is shown, for the camera.

| Key | Format | What the code holds | Android | iPhone |
|---|---|---|---|---|
| 1 | GOOGLE | `https://maps.google.com/?q=48.123333,-1.234444` | Google Maps app, or the browser, with a marker | Google Maps app if installed, else Safari |
| 2 | APPLE | `https://maps.apple.com/?q=48.123333,-1.234444` | The browser (Apple Maps on the web) | Apple Maps, with a pin |
| 3 | GEO | `geo:48.123333,-1.234444` | The map app chooser (Google Maps, Organic Maps, OsmAnd...), offline maps included | Not reliable: the Camera may show it as text only |
| 4 | OSM | `https://osm.org/?mlat=48.123333&mlon=-1.234444` | The browser: OpenStreetMap with a marker | Same, in Safari |
| 5 | TEXT | `48.123333,-1.234444` | The coordinates as text, to copy into any app | Same |

Both phones' cameras open `https://` links directly, so GOOGLE, the default,
works everywhere. GEO is the one for offline map apps on Android, but how a
scanner handles `geo:` depends on the scanner: scanner apps of the ZXing family
offer to open a map, the camera or Google Lens may show the text only.

Launched from the Apps menu, or with a message without a valid position, QR
Map shows a page explaining where the position comes from; EXIT quits.

## Accuracy

The position is never converted on the way: the sender hands its own
centi-arcseconds over. QR Map writes decimal degrees with 6 decimals,
rounded to the nearest: the text is within 0.5e-6 degree (5 cm) of the
position received. For comparison, a 406 MHz beacon resolves 4 seconds of arc
(about 120 m) at best, and the sonde decoders keep 0.01 second (0.3 m).

## Hand-off message

The sending app calls `handoff("QR Map", msg, len)` and quits. The message,
little-endian:

| Bytes | Field |
|---|---|
| 0-3 | Latitude, `int32_t`, centi-arcseconds (1/100 second of arc), north positive, ±90 degrees (±32,400,000) |
| 4-7 | Longitude, `int32_t`, centi-arcseconds, east positive, ±180 degrees (±64,800,000) |
| 8-31 | Optional label, ASCII, up to the end of the message or a NUL; 15 characters are shown |

Centi-arcseconds are the unit the sonde decoders (M10, M20, RS41) and EPIRB
406 already keep their positions in (`latS`, `lonS`), so a sender passes its
own fields as they are. A message shorter than 8 bytes or out of range shows
the "no position" page.

On EXIT QR Map hands off back to the sender, without a message: the sender
starts again as when launched from the menu.

A sender whose record has the position and the ID side by side (latitude,
longitude, then the ID, as EPIRB 406 does) passes the record itself. Otherwise
it copies them first, for example from the sondes' `sonde_info_t` (`id[12]`
before `latS` / `lonS`):

```c
const sonde_info_t *in = &g.info;
if (in->flags & SONDE_HAS_POS) {
    int32_t m[2 + sizeof in->id / 4];          /* latitude, longitude, ID */
    m[0] = in->latS;
    m[1] = in->lonS;
    memcpy(m + 2, in->id, sizeof in->id);
    if (g.A->handoff("QR Map", m, sizeof m))   /* false: QR Map not installed */
        quit();                                /* the loader launches QR Map */
}
```

## The code

`qr.c` encodes one fixed size: version 3 (29 × 29 modules), error correction
level L, byte mode, one block of 55 data and 15 error correction codewords, so
up to 53 bytes. The longest text, an OSM or Google link at -90 / -180
degrees, is 49 bytes. The encoder follows ISO/IEC 18004: bit stream,
Reed-Solomon over GF(256), function patterns, zigzag placement, then the mask
with the lowest penalty score and its format information. A row of the
symbol is one `uint32_t`, so the symbol and its function map take 232 bytes of
stack; there is no division (the masks keep `x mod 3` as counters).

On screen, each module is 2 × 2 pixels: 58 × 58 pixels at (4, 3), status line
included, about 13 mm wide on the LCD. The quiet zone is 1.5 modules above and
below (the screen is 64 pixels high), 2 on the left and 3 on the right before
the text. Scanners usually accept that, and the glass around the pixels
widens it.

## Size

**2,540 bytes** of the 4,096-byte overlay, plus 327 bytes of assets (texts and
the five formats).

## Tests (`test/`)

```
test/run_tests.sh
```

Needs a host C compiler, python3, and the `qrcode`, `opencv-python` and
`zxing-cpp` packages.

- `host_qr.c` + `check_qr.py`: 289 codes (every length from 0 to 53, random
  bytes, the five formats at the coordinate extremes) are bit for bit the ones
  the `qrcode` library makes with the same version, level and mask. The map
  codes, drawn as on the LCD with text beside them, decode at three scales
  with OpenCV and with ZXing, the decoder of many phone scanner apps. With a
  dark bezel right at the screen edge, ZXing still reads 105 of 105 and
  OpenCV 103 of 105: the radio test tells how much margin the real LCD
  leaves.
- `host_app.c` + `check_app.py`: the whole app on a stub API. The codes
  decoded from the frame buffer (OpenCV and ZXing must agree) for each format
  and key, the text beside them, the saved format, the return hand-off, the
  "no position" page, the labels, a message built as above from a sonde-like
  record, and the rounding of every case against exact fractions. Through
  `../../epirb406/test/host_dec406 -m`, all 36 T.001 frames with a position
  go from EPIRB 406's key 7 message to the code: each one holds the decoded
  position exactly, rounded to 1e-6 degree, with the beacon ID as the label.

## Credits

- The encoder (`qr.c`) follows the structure of Project Nayuki's
  [QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library)
  (MIT License): Reed-Solomon divisor and remainder, function patterns, format
  bits, zigzag placement, mask penalty. It is rewritten for one fixed version,
  `uint32_t` rows and no division; its MIT notice is in `qr.c`.
- The symbol is specified by ISO/IEC 18004:2015. QR Code is a registered
  trademark of DENSO WAVE INCORPORATED.
- The tests use the [`qrcode`](https://github.com/lincolnloop/python-qrcode)
  Python library as the reference encoder, and
  [OpenCV](https://opencv.org) and [zxing-cpp](https://github.com/zxing-cpp/zxing-cpp)
  as decoders; none of their code is in the app.
- `build.sh` and `app.ld` are the repository's overlay-app templates.

## Version

`APP_VER` in `build.sh` is bumped for every build that goes on a radio.
Current source: **v1.0**.
