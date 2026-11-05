#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "ui/gui_style.h"

int main(void)
{
    // All old firmware-produced display bytes keep their TINY/CLASSIC meaning.
    for (unsigned old = 0; old < 256; ++old) {
        if (!(old & 0x20))
            assert(GUI_STYLE_Decode(old) == ((old >> 7) & 1));
    }
    for (unsigned style = 0; style < GUI_STYLE_COUNT; ++style) {
        for (unsigned contrast = 0; contrast < 16; ++contrast) {
            for (unsigned inv = 0; inv < 2; ++inv) {
                for (unsigned meter = 0; meter < 2; ++meter) {
                    const uint8_t other = contrast | (inv << 4) | (meter << 6);
                    const uint8_t saved = other | GUI_STYLE_Encode(style);
                    assert(GUI_STYLE_Decode(saved) == style);
                    assert((saved & 0x5F) == other);
                    // Older firmware sees either enhanced style as CLASSIC.
                    assert(((saved >> 7) & 1) == (style != GUI_STYLE_TINY));
                    // Saving a different style clears its predecessor's flag.
                    for (unsigned next = 0; next < GUI_STYLE_COUNT; ++next) {
                        uint8_t changed = (saved & 0x5F) | GUI_STYLE_Encode(next);
                        assert(GUI_STYLE_Decode(changed) == next);
                        assert((changed & 0x5F) == other);
                    }
                }
            }
        }
    }
    // No invalid enum value can overwrite meter, inversion or contrast bits.
    for (unsigned invalid = GUI_STYLE_COUNT; invalid < 256; ++invalid)
        assert(GUI_STYLE_Encode(invalid) == 0);
    // A cleared legacy flag remains TINY even if an external tool left bit 5 set.
    assert(GUI_STYLE_Decode(0x20) == GUI_STYLE_TINY);
    assert(GUI_STYLE_Decode(0xFF) == (UI_PLUS_ENABLED ? GUI_STYLE_PLUS : GUI_STYLE_CLASSIC));
    printf("PASS: %u styles round-trip, legacy values and other display settings preserved, invalid styles sanitized.\n", GUI_STYLE_COUNT);
}
