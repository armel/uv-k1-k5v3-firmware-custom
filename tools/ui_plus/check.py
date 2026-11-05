#!/usr/bin/env python3
"""Render the real main screen on the host; optionally compare a git baseline.

After compiling Fusion:
  python3 tools/ui_plus/check.py --baseline-ref <commit-before-ui-plus>
Requires a host C compiler (Clang on macOS). No radio or Python packages needed.
"""
import argparse
from functools import lru_cache
import itertools
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/Fusion-UI-Plus/review'
PREVIEW = ROOT / 'preview/review'


def run(args, **kwargs):
    return subprocess.run(args, cwd=ROOT, check=True, **kwargs)


def png(frames, destination):
    # Left: baseline; right: UI Plus. Main framebuffer only (status bar unchanged).
    scale = 5
    width, height = len(frames) * 128 + (len(frames) - 1) * 8, 56
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            frame, col = divmod(x, 136)
            color = (255, 255, 255)
            if col < 128:
                on = frames[frame][y // 8 * 128 + col] & (1 << (y % 8))
                color = (20, 29, 23) if on else (197, 214, 176)
            row.extend(bytes(color) * scale)
        rows.extend([b'\0' + row] * scale)

    def chunk(kind, data):
        return (struct.pack('!I', len(data)) + kind + data +
                struct.pack('!I', zlib.crc32(kind + data)))

    destination.write_bytes(
        b'\x89PNG\r\n\x1a\n' +
        chunk(b'IHDR', struct.pack('!IIBBBBB', width * scale, height * scale, 8, 2, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref')
    parser.add_argument('--previous-main', help='Optional prior main.c snapshot for scoped regression checks')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    PREVIEW.mkdir(parents=True, exist_ok=True)
    commands = json.loads((ROOT / 'build/Fusion/compile_commands.json').read_text())
    command = next(c for c in commands if c['file'].endswith('/App/ui/main.c'))
    flags = [a.replace('/src/', str(ROOT) + '/')
             for a in shlex.split(command['command']) if a.startswith(('-I', '-D'))]
    compiler = os.environ.get('CC', 'clang')
    flags += ['-std=gnu11', '-O1', '-g', '-IApp/ui', '-Wno-pointer-to-int-cast',
              '-Wno-c23-extensions', '-ffunction-sections', '-fdata-sections',
              '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    linker = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
    settings_test = str(OUT / 'style-settings-test')
    run([compiler, *flags, 'tools/ui_plus/style_settings_test.c', '-o', settings_test])
    run([settings_test])
    disabled_settings = str(OUT / 'style-settings-disabled-test')
    disabled_flags = [a for a in flags if not a.startswith('-DENABLE_FEAT_F4HWN_UI_PLUS')]
    run([compiler, *disabled_flags, 'tools/ui_plus/style_settings_test.c', '-o', disabled_settings])
    run([disabled_settings])
    sources = ['App/ui/helper.c', 'App/font.c', 'App/bitmaps.c', 'App/misc.c',
               'App/ui/ui_plus.c', 'App/dcs.c', 'App/ui/inputbox.c',
               'App/external/printf/printf.c', 'tools/ui_plus/render.c']
    objects = []
    for i, source in enumerate(sources):
        obj = OUT / ('common-%d.o' % i)
        # The legacy small printer intentionally addresses the next LCD page
        # via Start > 127. Keep ASan there, but don't flag its subarray arithmetic.
        legacy_flags = ['-fno-sanitize=array-bounds'] if source == 'App/ui/helper.c' else []
        run([compiler, *flags, *legacy_flags, '-c', source, '-o', str(obj)])
        objects.append(str(obj))
    variants = ['plus']
    if args.baseline_ref:
        baseline = run(['git', 'show', args.baseline_ref + ':App/ui/main.c'], capture_output=True).stdout
        (OUT / 'baseline.c').write_bytes(baseline)
        variants.insert(0, 'baseline')
    for variant in variants + (['previous'] if args.previous_main else []):
        source = args.previous_main if variant == 'previous' else ('App/ui/main.c' if variant == 'plus' else str(OUT / 'baseline.c'))
        obj = OUT / (variant + '.o')
        run([compiler, *flags, '-include', 'tools/ui_plus/host_gpio.h', '-c', source, '-o', str(obj)])
        run([compiler, *flags, *objects, str(obj), linker, '-o', str(OUT / variant)])

    def render_actual(variant, values):
        return render_cached(variant, tuple(values))

    @lru_cache(maxsize=None)
    def render_cached(variant, values):
        try:
            frame = run([str(OUT / variant), *map(str, values)], capture_output=True).stdout
        except subprocess.CalledProcessError as error:
            print(error.stderr.decode(), file=sys.stderr)
            raise
        assert len(frame) == 896, values
        return frame

    def render(variant, values):
        # Existing geometry tests use logical 1 for the enhanced layout.
        # Its actual SetGUI value is now 2; baseline/previous CLASSIC used 1.
        values = list(values)
        if variant == 'plus' and values[0] == 1:
            values[0] = 2
        return render_actual(variant, values)

    # TINY/CLASSIC match upstream; UI+ matches r13 except the new single FREQ and CHANNEL NUMBER views.
    style_cases = 0
    cases = list(itertools.product([0, 1], range(4), [0, 1], [0, 1, 4], [0, 1]))
    cases += [(dual, 3, active, function, 0, scenario)
              for dual, active, function, scenario in
              itertools.product([0, 1], [0, 1], [0, 1, 4], range(25))]
    cases += [(dual, 3, active, function, 0, scenario, 40, meter, style)
              for dual, active, (function, meter), style, scenario in
              itertools.product([0, 1], [0, 1], [(1, 0), (1, 1), (1, 2), (4, 3)], [0, 1], [0, 7, 13, 18])]
    for values in cases:
        if args.baseline_ref:
            for gui in (0, 1):
                assert render_actual('plus', [gui, *values]) == render_actual('baseline', [gui, *values]), ('original style changed', gui, values)
                style_cases += 1
        single_vfo = values[0] == 0 and len(values) > 5 and (
            (values[5] == 5 and values[2] == 1) or (values[5] == 19 and values[2] == 0))
        new_dual = values[0] != 0 and (values[1] != 3 or (len(values) > 5 and values[5] in (5, 19)))
        if args.previous_main and not single_vfo and not new_dual and tuple(values[:2]) not in ((0, 0), (0, 1), (0, 2)):
            assert render_actual('plus', [2, *values]) == render_actual('previous', [1, *values]), ('UI+ changed since r13', values)
            style_cases += 1
    print('PASS: %d full-frame style comparisons, including live meters: TINY/CLASSIC match upstream; unchanged UI+ views match the previous revision.' % style_cases)

    unchanged = targeted = 0
    previous_unchanged = 0
    for values in itertools.product([0, 1], [0, 1], range(4), [0, 1], [0, 1, 4], [0, 1]):
        gui, dual, mdf, active, function, monitor = values
        current = render('plus', values)
        if args.baseline_ref:
            baseline = render('baseline', values)
            if gui == 1 and dual == 1:
                assert current != baseline, values
                assert all(not (b & 0x07) for b in current[:128]), ('A not lowered', values)
                if function != 4:
                    assert all(not (b & 0xf8) for b in current[384:512]), ('A reaches too far into gap', values)
                targeted += 1
            elif gui == 1 and dual == 0 and mdf in (0, 1, 2, 3):
                assert current != baseline, ('single layout missing', values)
                targeted += 1
            else:
                assert current == baseline, ('out-of-scope screen changed', values)
                unchanged += 1
        if args.previous_main and (gui == 0 or (dual == 1 and mdf == 3)):
            assert current == render('previous', values), ('unrelated screen changed since previous revision', values)
            previous_unchanged += 1

    for scenario, active, function in itertools.product(range(19), [0, 1], [0, 1, 4]):
        values = [1, 1, 3, active, function, 1, scenario]
        current = render('plus', values)
        lowered = scenario not in (7, 11, 18) and not (scenario in (12, 14) and function != 1)
        if args.baseline_ref and not lowered:
            baseline = render('baseline', values)
            assert current[384:512] == baseline[384:512], ('occupied middle line changed', values)
        if args.previous_main and scenario != 5 and (function != 4 or not lowered):
            assert current == render('previous', values), ('scan/message layout changed since revision 6', values)
            previous_unchanged += 1
        # The lock belongs above the badge, clear of the active arrow and name.
        # Its normal RX suppression remains unchanged for the receiving VFO.
        lock = [0x3c, 0x26, 0x25, 0x25, 0x25, 0x26, 0x3c]
        for vfo in (0, 1):
            if scenario == 5 and vfo == 1:
                continue  # Frequency-mode VFO retains its existing layout.
            if (function == 4 or scenario in (7, 18)) and vfo == active:
                continue  # RX/audio label replaces the lock by existing policy.
            y0 = vfo * 32 + (3 if vfo == 0 and lowered else 0)
            for col, bits in enumerate(lock):
                for row in range(6):
                    y = y0 + row
                    actual = bool(current[y // 8 * 128 + 8 + col] & (1 << (y % 8)))
                    assert actual == bool(bits & (1 << row)), ('lock overlaps another indicator', values)

        # A's whole block moves only when the shared line is free; B stays put.
        for start in (0, 32):
            if scenario == 5 and start == 32:  # Unchanged frequency-mode VFO.
                continue
            y0 = start + (3 if start == 0 and lowered else 0)
            graph = scenario == 18 and function != 4 and start // 32 == active
            # A clear column separates long RX labels (or the scan graph) from names.
            gap_x = 31 if graph else 27
            for y in range(y0, y0 + 10):
                assert not (current[y // 8 * 128 + gap_x] & (1 << (y % 8))), ('name touches indicator', values)
            for y in (start + 18 + (3 if start == 0 and lowered else 0),):
                assert all(not (current[y // 8 * 128 + x] & (1 << (y % 8)))
                           for x in range(128)), ('overlapping status strip', values, y)
        if active == 0 and function == 0:
            frames = [render(v, values) for v in variants]
            png(frames, PREVIEW / ('scenario-%02d.png' % scenario))
    for name, values in [('idle', [1, 1, 3, 0, 0, 0]), ('tx-b', [1, 1, 3, 1, 1, 0]),
                         ('rx-b', [1, 1, 3, 1, 4, 0])]:
        png([render(v, values) for v in variants], PREVIEW / (name + '.png'))

    def pixel(frame, x, y):
        return bool(frame[y // 8 * 128 + x] & (1 << (y % 8)))

    # Refresh both meter styles on either TX VFO, from silence through clipping
    # and decay. Overlay apps, single-channel CLASSIC and TINY retain full height.
    meter_cases = 0
    for gui, dual, active, meter, style in itertools.product([0, 1], [0, 1], [0, 1], range(3), [0, 1]):
        values = [gui, dual, 3, active, 1, 0, 13]
        before = render('plus', values)
        after = render('plus', [*values, 40, meter, style])
        compact = gui == 1 and dual == 1 and meter != 2
        line = 3 if dual else 5
        if compact:
            for x in range(128):
                assert (before[384 + x] & 0x07) == (after[384 + x] & 0x07), ('TX meter erases A status', values, meter)
                assert not (after[384 + x] & 0x88), ('TX meter touches adjacent rows', values, meter)
            assert any(b & 0x70 for b in after[384:512]), ('TX meter missing', values, meter)
            idle = render('plus', [gui, dual, 3, active, 0, 0, 13])
            returned = render('plus', [*values, 40, meter, style, 1])
            assert returned == idle, ('TX release changes idle layout', values, meter)
            for y in range(3, 13):
                for x in range(28, 108):
                    assert pixel(idle, x, y) == pixel(after, x, y), ('A name jumps on TX', values)
        elif gui == 1 and dual == 0 and meter != 2:
            for y in range(56):
                for x in range(128):
                    if 35 <= y <= 40:
                        continue
                    assert pixel(before, x, y) == pixel(after, x, y), ('single TX damages text', values, meter, x, y)
            assert any(pixel(after, x, y) for y in range(35, 41) for x in range(128))
            returned = render('plus', [*values, 40, meter, style, 1])
            idle = render('plus', [gui, dual, 3, active, 0, 0, 13])
            assert returned == idle, ('single TX release changes idle layout', values, meter)
        elif args.baseline_ref:
            original = render('baseline', [*values, 40, meter, style])
            assert after[line * 128:(line + 1) * 128] == original[line * 128:(line + 1) * 128], ('full-height meter changed', values, meter)
        meter_cases += 1
    # Mixed VFO/memory views now use the same compact meters as other UI+ dual views.
    for meter in (0, 1):
        values = [1, 1, 3, 1, 1, 0, 19, 40, meter, 0]
        frame = render('plus', values)
        assert all(not (b & 0x88) for b in frame[384:512]), ('mixed VFO meter too tall', meter)
        meter_cases += 1
    png([render('plus', [1, 1, 3, 0, 0, 0, 13]),
         render('plus', [1, 1, 3, 0, 1, 0, 13, 40, 0, 0])], PREVIEW / 'idle-vs-tx.png')

    rx_cases = 0
    # Exercise every RX segment boundary through the real dBm conversion and
    # live redraw. The first five columns are low, next four medium, last four
    # full-height; all segments solid. Values below S1 stay empty and above +40 saturate.
    thresholds = [-142, -141, -135, -129, -123, -117, -111, -105,
                  -99, -93, -83, -73, -63, -53, -43]
    for style, active, (level, dbm) in itertools.product([0, 1], [0, 1], enumerate(thresholds)):
        values = [1, 1, 3, active, 4, 0, 0, 40, 3, style, 0, dbm]
        current = render('plus', values)
        for segment in range(13):
            for col in range(5):
                expected = 0
                if segment < min(level, 13) and col < 4:
                    expected = 0x40 if segment < 5 else 0x60 if segment < 9 else 0x70
                assert current[384 + 62 + segment * 5 + col] & 0xf8 == expected, ('RX height/threshold mismatch', values, segment, col)
        if args.previous_main:
            previous = render('previous', values)
            for y in range(56):
                for x in range(128):
                    if 62 <= x < 127 and 28 <= y <= 30:
                        continue
                    assert pixel(current, x, y) == pixel(previous, x, y), ('RX change outside bar', values, x, y)

    for gui, dual, active, function, style in itertools.product([0, 1], [0, 1], [0, 1], [2, 3, 4], [0, 1]):
        values = [gui, dual, 3, active, function, 0, 0]
        before = render('plus', values)
        after = render('plus', [*values, 40, 3, style])
        if gui == 1 and dual == 1:
            idle = render('plus', [1, 1, 3, active, 0, 0, 0])
            for y0 in (3, 32):
                for y in range(y0, y0 + 10):
                    for x in range(28, 107):
                        assert pixel(idle, x, y) == pixel(after, x, y), ('channel name jumps on RX', values)
            for y in range(56):
                if 27 <= y < 32:
                    continue
                for x in range(7, 128):
                    assert pixel(before, x, y) == pixel(after, x, y), ('live RSSI erases channel text', values, x, y)
            # No stale arrow is left at A's old position during live blinking.
            assert all((after[x] & 0x07) == 0 for x in range(7)), ('RX arrow jumps above A', values)
            returned = render('plus', [*values, 40, 3, style, 1])
            marker_y = 4 if active == 0 else 33
            for y in range(56):
                for x in range(128):
                    if 20 <= x <= 26 and marker_y <= y < marker_y + 5:
                        continue  # The existing last-received >> marker appears on release.
                    assert pixel(returned, x, y) == pixel(idle, x, y), ('RX release shifts idle layout', values, x, y)
        elif gui == 1 and dual == 0:
            idle = render('plus', [1, 0, 3, active, 0, 0, 0])
            for y in range(56):
                for x in range(128):
                    if 35 <= y <= 40 or (x < 7 and 23 <= y <= 29):
                        continue
                    assert pixel(before, x, y) == pixel(after, x, y), ('single RX damages text', values, x, y)
            for y in range(1, 19):
                for x in range(21, 110):
                    assert pixel(idle, x, y) == pixel(after, x, y), ('single name moves on RX', values)
            assert render('plus', [*values, 40, 3, style, 1]) == idle, ('single RX release changes idle layout', values)
        elif args.baseline_ref:
            assert after == render('baseline', [*values, 40, 3, style]), ('legacy receive screen changed', values)
        rx_cases += 1
    if args.previous_main:
        for active, scenario, function in itertools.product([0, 1], [7, 11, 12, 14, 18], [2, 3, 4]):
            values = [1, 1, 3, active, function, 0, scenario, 40, 3, 0]
            assert render('plus', values) == render('previous', values), ('existing scan/prompt RX behavior changed', values)
            previous_unchanged += 1
    png([render('plus', [1, 1, 3, 0, 0, 0]),
         render('plus', [1, 1, 3, 0, 4, 0, 0, 40, 3, 0])], PREVIEW / 'idle-vs-rx.png')
    single_cases = 0
    for scenario, active, function in itertools.product(range(25), [0, 1], [0, 1, 4]):
        values = [1, 0, 3, active, function, 0, scenario]
        if (scenario == 5 and active == 1) or (scenario == 19 and active == 0):
            continue  # VFO now uses the large frequency layout, checked below.
        current = render('plus', values)
        fallback = scenario in (1, 2, 6, 14, 22)
        if fallback and args.previous_main:
            assert current == render('previous', values), ('fallback changed', values)
        elif not fallback:
            # No name/status/frequency row can reach its neighboring text row.
            for y in (0, 19, 20, 31, 32, 33, 34, 41, 42, 48, 55):
                # VFO badge retains its existing seven-pixel border at y48.
                width = 105 if y == 48 else 128
                assert all(not pixel(current, x, y) for x in range(width)), ('single rows overlap', values, y)
            if function == 4:
                live = render('plus', [*values, 40, 3, 0])
                for y in range(56):
                    for x in range(128):
                        if 35 <= y <= 40 or (x < 7 and 23 <= y <= 29):
                            continue
                        assert pixel(current, x, y) == pixel(live, x, y), ('single live update damages text', values, x, y)
        single_cases += 1
    numeric_cases = 0
    for mdf, scenario, active, function in itertools.product([0, 1, 2], range(25), [0, 1], [0, 1, 4]):
        values = [1, 0, mdf, active, function, 0, scenario]
        current = render('plus', values)
        fallback = scenario in (1, 2, 6, 14, 22)
        if fallback and args.previous_main:
            assert current == render('previous', values), ('single numeric fallback changed', values)
        elif not fallback:
            # Header ends at y7, large readouts occupy y11..32. Activity, status
            # and footer reuse the tested NAME+FREQ rows below them.
            for y in (8, 9, 10, 33, 34, 41, 42, 48, 55):
                width = 105 if y == 48 else 128
                assert all(not pixel(current, x, y) for x in range(width)), ('single numeric rows overlap', values, y)
            assert any(pixel(current, x, y) for x in range(4, 124) for y in range(11, 33)), ('large numeric readout missing', values)
            for meter, style in itertools.product(([0, 1] if function == 1 else [3]) if function in (1, 4) else [], [0, 1]):
                live = render('plus', [*values, 40, meter, style])
                for y in range(56):
                    for x in range(128):
                        if 35 <= y <= 40 or (function == 4 and x < 7 and 1 <= y <= 7):
                            continue
                        assert pixel(current, x, y) == pixel(live, x, y), ('single numeric live update damages text', values, x, y)
                if scenario in (0, 13, 20):
                    returned = render('plus', [*values, 40, meter, style, 1])
                    idle = render('plus', [1, 0, mdf, active, 0, 0, scenario])
                    assert returned == idle, ('single numeric release changes layout', values)
        numeric_cases += 1
    print('PASS: %d single-frequency/channel-number/name-only cases, including four-digit MHz/memories, scanning, messages, input fallbacks, and live RX/TX stability.' % numeric_cases)

    vfo_cases = 0
    for mdf, active, function, scenario in itertools.product(range(4), [0, 1], [0, 1, 4], [0, 1, 2, 6, 7, 11, 12, 13, 14, 18, 20, 22, 25, 26, 27]):
        values = [1, 0, mdf, active, function, 0, scenario, 0, 0, 1, 0, -93, 1, 1]
        current = render('plus', values)
        fallback = scenario in (1, 2, 6, 14, 22, 25, 26)
        if fallback and args.previous_main:
            assert current == render('previous', values), ('VFO entry/range/warning changed', values)
        elif not fallback:
            for y in (8, 9, 10, 33, 34, 41, 42, 48, 55):
                width = 105 if y == 48 else 128
                assert all(not pixel(current, x, y) for x in range(width)), ('VFO rows overlap', values, y)
            reference = values.copy()
            reference[2] = 0
            assert current == render('plus', reference), ('memory display preference changes VFO', values)
            for meter in ([0, 1] if function == 1 else [3] if function == 4 else []):
                live_args = values.copy()
                live_args[7:9] = [40, meter]
                live = render('plus', live_args)
                for y in range(56):
                    for x in range(128):
                        if 35 <= y <= 40 or (function == 4 and x < 7 and 1 <= y <= 7):
                            continue
                        assert pixel(current, x, y) == pixel(live, x, y), ('VFO refresh damages text', values, x, y)
        vfo_cases += 1
    print('PASS: %d VFO cases: all ChDisp preferences, both VFOs, live RX/TX, scanning, compander, input/warning/range fallbacks.' % vfo_cases)

    dual_cases = 0
    for mdf, source, active, function, scenario in itertools.product(range(4), [0, 1, 2], [0, 1], [0, 1, 4], [0, 3, 4, 13, 16, 18, 23]):
        values = [1, 1, mdf, active, function, 0, scenario, 0, 0, 1, 0, -93, 1, source]
        current = render('plus', values)
        shift = 0 if scenario == 18 else 3
        for y in (18 + shift, 50):
            assert all(not pixel(current, x, y) for x in range(128)), ('dual status overlaps readout', values, y)
        # Idle/TX leave a blank row before B. RX uses y27..31 for its
        # five-pixel readings; the live-update comparison below protects B.
        if shift and function != 4:
            assert all(not pixel(current, x, 31) for x in range(128)), ('dual TX gap missing', values)
        for meter in ([0, 1] if function == 1 else [3] if function == 4 else []):
            live_args = values.copy()
            live_args[7:9] = [40, meter]
            live = render('plus', live_args)
            for y in range(56):
                for x in range(128):
                    if 24 <= y <= 31 or (function == 4 and x < 7):
                        continue
                    assert pixel(current, x, y) == pixel(live, x, y), ('dual live update damages text', values, x, y)
        dual_cases += 1
    print('PASS: %d dual-view cases: all four formats, memory/VFO mixtures, both active channels, names, scanning, and live RX/TX.' % dual_cases)

    for name, scenario, function, frames, meter in [
        ('single-idle', 0, 0, 0, 0), ('single-simplex', 20, 0, 0, 0),
        ('single-scan', 18, 0, 0, 0), ('single-scan-list', 21, 0, 0, 0),
        ('single-rx', 18, 4, 40, 3), ('single-tx', 13, 1, 40, 0),
        ('single-four-digit', 0, 0, 0, 0)]:
        active = int(name == 'single-four-digit')
        frame = render('plus', [1, 0, 3, active, function, 0, scenario, frames, meter, 0])
        (OUT / (name + '.raw')).write_bytes(frame)
        png([frame], PREVIEW / (name + '.png'))
    print('PASS: %d single-channel edge cases, including channel entry, four-digit memories/frequencies, scan lists, DTMF and prompts.' % single_cases)
    print('PASS: 192 main-screen cases plus 114 edge cases; ASan enabled; UBSan enabled except legacy helper subarray bounds.')
    print('PASS: %d live TX meter cases; 40 refreshes per case; A stays aligned and its status rows survive.' % meter_cases)
    print('PASS: %d RX/monitor cases; 40 live updates per case; idle/RX/idle alignment and status pixels preserved.' % rx_cases)
    if args.previous_main:
        print('PASS: %d screens match the supplied previous revision, including scanning and existing message layouts.' % previous_unchanged)
    print('PASS: 60 compact RX threshold cases; three height tiers, solid over-S9 bars and saturation checked.')
    if args.baseline_ref:
        print('PASS: %d unchanged screens match baseline byte-for-byte; %d target screens changed.' %
              (unchanged, targeted))
    print('Previews:', PREVIEW)


if __name__ == '__main__':
    main()
