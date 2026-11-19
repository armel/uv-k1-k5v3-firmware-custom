# Experimental repeat of the captured selection

Based on the hardware-validated `experiment/cw-qso-auto-names16` working tree.
Integrated on `contribution/cw-qso-auto` for feature_update_v7; the stable tag remains unchanged.

The sole application change is at the shared launch path in `app_main`:

```c
q.repeat = key == KEY_SIDE1;
q.message = q.selected;
repeatLaunch:
startAuto();
```

Previously the `q.message` assignment was below `repeatLaunch` and selected
message 01 whenever repetition was enabled. Now initial SIDE 1/PTT activation
captures the current selection. A scheduled repeat jumps past the assignment
and reuses the existing `q.message`. No state or BSS was added.

The existing navigation remains usable during a pause. It updates the displayed
selection but does not replace the captured repeated message. SIDE 2 stops by
the existing rules. A fresh SIDE 1 after stopping captures the latest selection.
PTT during a pause still cancels repetition and sends the current selection.
At application entry the selection is 01, so the default remains message 01.

No other C source change relative to the named-message reference. No change
to Morse/RF/audio code, assets, names, CW models, delays or settings. `{RST}`
remains the only runtime token; callsign/name text remains literal.

Strict production build: overlay **4084**, BSS **92** (included), assets **2286**,
APP **6434** bytes. No optimization, compiler flag or assembly change needed.

The HTML adds the approved `names16-selected` binary profile and embeds the new
APP. The previous `names16` profile remains supported without changing its code
or data on import/export. Editor logic/layout/fields are unchanged; its help
sentence now accurately describes repetition of the selected message.

Reproduce with ARM_CC configured:

```
python App/apps/cwqsoauto/test/experimental.py --browser
```

`repeat_selected.py` adds 272 foreground checks: every selection, 1/5/15 second
delays in AUTO/A/B, repeated SIDE 1, pause navigation with frozen repeat target,
stop during pause/transmission, independent PTT after navigation, TX denial,
EXIT/paddle cancellation, empty/unsupported payloads and cycle-counter wrap.
All pre-existing relevant experimental suites are retained.

This revision needs radio validation; software tests do not establish physical
RF/audio performance or Flash persistence. Keep the previous APP for rollback.
