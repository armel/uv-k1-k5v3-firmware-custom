#!/usr/bin/env python3
"""Host model checks for APRSRX history; does not compile or execute the C app."""
from collections import deque
import random

HISTORY = 5


def check_rotation():
    rng = random.Random(330)
    slots = [None] * HISTORY
    order = list(range(HISTORY))
    count = 0
    expected = deque(maxlen=HISTORY)
    last_time = 0
    now = 0xFFFFFF00
    accepted = 0
    for _ in range(10000):
        if rng.randrange(20) == 0:
            count = 0
            expected.clear()
        now = (now + rng.choice((0, 50, 999, 1000, 2000))) & 0xFFFFFFFF
        if expected and rng.randrange(3) == 0:
            payload = expected[0][0]
        else:
            payload = rng.randbytes(rng.choice((18, 19, 233, 330)))
        record = (payload, rng.randrange(-130, -20))
        duplicate = bool(expected and len(payload) == len(expected[0][0])
                         and payload[-2:] == expected[0][0][-2:]
                         and (now - last_time) & 0xFFFFFFFF < 1000)
        newest = slots[order[0]]
        reject = bool(count and len(payload) == len(newest[0])
                      and payload[-2:] == newest[0][-2:]
                      and (now - last_time) & 0xFFFFFFFF < 1000)
        assert reject == duplicate
        if not reject:
            recycled = order[HISTORY - 1]
            for k in range(HISTORY - 1, 0, -1):
                order[k] = order[k - 1]
            order[0] = recycled
            slots[recycled] = record
            count = min(count + 1, HISTORY)
            expected.appendleft(record)
            accepted += 1
        last_time = now
        assert sorted(order) == list(range(HISTORY))
        assert [slots[k] for k in order[:count]] == list(expected)
    print(f'History: 10000 events, {accepted} accepted; rotation, clear, duplicates, tick wrap OK')


def check_scroll():
    # One frame at a time: the source on line 0, body rows from y = 8, scrolled
    # by top. Transcription of emit() and of draw()'s limit.
    for step in (6, 8):
        for rows in range(1, 60):
            end = 8 + rows * step                   # g.vrow after the last row
            lim = end - 32 if end > 32 else 0
            assert end < 65536
            for top in range(lim + 1):
                drawn = []
                for r in range(rows):
                    y = (8 + r * step - top) & 0xFFFFFFFF
                    if (y - 1) & 0xFFFFFFFF < 30:
                        drawn.append(y)
                    else:
                        # Skipped rows lie wholly under the source (y <= 0, at
                        # most 8 px high) or wholly below the separator.
                        signed = 8 + r * step - top
                        assert signed <= 0 or signed >= 31
                # No row is cut by the separator at rest positions the scroll can
                # reach from the top: the first body row starts right under the source.
                if top == 0:
                    assert drawn[0] == 8
            # At the limit the last glyph row (ending one pixel before its row
            # height) is the last one above the y = 31 separator.
            assert end - 2 - lim <= 30
            if lim:
                assert end - 2 - lim == 30
    print('Scroll: both fonts, 1 to 59 body rows, every pixel offset; clipping and limit OK')


def check_keys():
    # Transcription of handleKeys(): held UP/DOWN scrolls inside the frame; a
    # new press at an end changes frame. Raw UP/LEFT moves towards the start and
    # raw DOWN/RIGHT towards the end, in either SET_NAV mode.
    for set_nav in (False, True):
        for key, raw in (('UP', -1), ('DOWN', 1)):
            nav = (1 if key == 'UP' else -1) * (1 if set_nav else -1)
            assert raw == (-nav if set_nav else nav)
    rng = random.Random(59)
    for _ in range(300):
        count = rng.randrange(1, HISTORY + 1)
        lims = [rng.choice((0, 0, 3, 40)) for _ in range(count)]
        cur = top = 0
        prev = None
        for _ in range(400):
            key = rng.choice((None, 'UP', 'DOWN', 'UP', 'DOWN', 'OTHER'))
            if rng.randrange(3):
                key = prev if prev else key          # mostly held keys
            d = {'UP': -1, 'DOWN': 1}.get(key, 0)
            before = (cur, top)
            lim = lims[cur]
            moved = (top + d) & 0xFFFFFFFF
            if moved <= lim:
                top = moved
            elif key != prev and (cur + d) & 0xFFFFFFFF < count:
                cur = (cur + d) & 0xFF
                top = 0
            # Oracle.
            if d == 0:
                assert (cur, top) == before
            elif 0 <= before[1] + d <= lim:
                assert (cur, top) == (before[0], before[1] + d)
            elif key == prev or not 0 <= before[0] + d < count:
                assert (cur, top) == before          # held at an end, or no such frame
            else:
                assert (cur, top) == (before[0] + d, 0)
            assert 0 <= cur < count and 0 <= top <= lims[cur]
            prev = key
    print('Keys: held scroll stays in the frame, a new press at an end changes frame OK')


def check_columns():
    # Compare the C scratch-column shift against a per-pixel clipping oracle.
    for y in range(1, 31):
        for glyph in range(256):
            bits = (glyph << y) & 0xFFFFFFFF
            pages = [0] * 4
            page = 0
            while bits:
                assert page < 4
                pages[page] |= bits & 255
                bits >>= 8
                page += 1
            packed = sum(value << (8 * page) for page, value in enumerate(pages))
            expected = sum(1 << (y + bit) for bit in range(8)
                           if glyph & (1 << bit) and y + bit < 32)
            assert packed == expected
    print('Scratch renderer: all 256 column patterns, 30 offsets, clipping OK')


if __name__ == '__main__':
    check_rotation()
    check_scroll()
    check_keys()
    check_columns()
