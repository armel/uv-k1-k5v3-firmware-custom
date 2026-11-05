# Resistor 0.1

A resistor colour-code calculator overlay for F4HWN Labs ABI 1 / API 2, after
the Flipper Zero [resistors](https://github.com/xMasterX/all-the-plugins/tree/dev/apps_source_code/resistors)
app by Lewis Westbury (instantiator, MIT licence). No radio reception or transmission is needed.

Pick a 3, 4, 5 or 6 band resistor and set its bands. The screen shows the
value in the small bold font with its SI prefix (`4.7 kΩ`, `0.47 Ω`,
`100 GΩ`), the tolerance in the 3x5 font (and the temperature coefficient with
6 bands), and the resistor with its bands. Under each band: its three-letter
colour code, in a capsule for the selected band, and for the digits and the
multiplier what the band contributes (`4`, `7`, `x100`).

| Code | Colour | Code | Colour | Code | Colour |
| --- | --- | --- | --- | --- | --- |
| BLK | black | YEL | yellow | GRY | grey |
| BRN | brown | GRN | green | WHT | white |
| RED | red | BLU | blue | GLD | gold |
| ORG | orange | VIO | violet | SLV | silver |

The selected band is drawn black.

| Key | Action |
| --- | --- |
| Arrows | Select the previous / next band, wrapping; hold to repeat |
| * | Next colour of the selected band; hold to repeat |
| F, then * | Previous colour; hold * to repeat. F shows an `F` capsule in the status line until the next key; F again cancels it |
| 0 - 9 | Set the selected band to that colour code, then select the next band |
| MENU | 3 → 4 → 5 → 6 → 3 bands |
| EXIT | Exit |

The arrows follow the navigation direction setting, like the rest of the
firmware.

Each band only offers the colours its role accepts: digits black to white,
multiplier black to silver (×1 to ×10⁹, gold ×0.1, silver ×0.01), tolerance
and temperature coefficient as in IEC 60062:2016. Typing digits is a quick way
to find the bands of a value: on a 4 band resistor, `4` `7` `2` gives yellow
violet red, 4.7 kΩ. A digit that the band does not accept (0 on the tolerance
band) is ignored.

Going from 4 to 5 bands keeps the value (a third digit 0, the multiplier one
step lower); going from 6 to 3 bands keeps it too when the third digit is 0.
A 3 band resistor has no tolerance band: ±20 %.

Differences from the Flipper app: the IEC 60062:2016 tables (orange ±0.05 %,
yellow ±0.02 %, grey ±0.01 % tolerances; black, green and grey temperature
coefficients), the value printed with its SI prefix rather than `47 x 10^2`,
each band's contribution under it, direct colour entry with the digits, and
the band count changed with MENU instead of a start menu.

The band count and the colours are saved on exit through the resident deferred
configuration service.

## Build and tests

```sh
./compile-app.sh resistor
python3 App/apps/resistor/test/model.py
```

`test/model.py` is a Python model of the app: it checks the value formatter
against an exact reference over every 4 and 5 band combination, the keys and
the band-count changes, and renders LCD mockups into `test/out/`.
