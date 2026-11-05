# CW USB deterministic host tests

Build CW USB normally to generate its assets, then run from the repository root:

```sh
python App/apps/cwusb/test/arming.py
python App/apps/cwusb/test/functional.py
python App/apps/cwusb/test/waveforms.py
```

Python 3 and a C11 compiler are required (`HOST_CC`, default `gcc`). Output is
written under ignored `test/out*/`. Tests compile the actual app C; they replace
MMIO, IRQ handling and API callbacks with deterministic simulation. The ARM
build separately checks the real API layout and 4096-byte link limit.

- Arming: 41 AUTO controls, retained from the first contribution commit.
- Functional: 1277 controls, including the historical 1250 and 27 additional
  Ready/UI/STRAIGHT cases. Real resident font/render functions are extracted
  from the checkout to check visible pixels, not only print callbacks.
- Waveforms: 144 AUTO/A/B scenarios; historical traces and times in the frozen
  reference JSON are compared where available (39 per algorithm). Busy-mode
  changes are rejected and idle selection is checked.

The 1277 count includes the replayed 1250; do not add them as distinct cases.
API3 tests use the 368-byte prefix even if the checkout has later appended
services. No test proves physical RF silence, electrical contact detection or
maximum stack usage. The simulation sources and trace fixtures originate from
this contribution's development, not additional device measurements.
