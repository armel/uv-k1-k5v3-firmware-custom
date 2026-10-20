# CW Keyer host checks

These tests compile the actual application C source with simulated MMIO and
API callbacks. They run outside the 4 KiB overlay. See the parent README for
build prerequisites and commands. Run `test_keyer.py` first and pass the same
`--output-dir` to `test_persistence.py`; the latter includes its generated C
harness. `HOST_CC` selects a GCC-compatible host compiler, `ARM_NM` the matching
ARM symbol utility. No Python packages beyond the standard library are needed.

## Expected waveform sequences

The time quantum `d` is `floor(1200/WPM)` ms. Each row is replayed at 5, 18
and 60 WPM, in all three modes. `.` is DIT and `-` is DAH.

| Stimulus | AUTO | IAMB A | IAMB B |
|---|---|---|---|
| DIT held until 5d | `...` | `...` | `...` |
| DAH held until 9d | `---` | `---` | `---` |
| Both held until 7d | `.-.` | `.-.` | `.-.-` |
| Initial squeeze, release at d/2 | `.` | `.` | `.-` |
| DAH, squeeze at d/4, release at d | `-` | `-` | `-.` |
| Initial squeeze, release at 1.5d | `.` | `.` | `.-` |
| Squeeze only at 1.25d to 1.75d | `.` | `.` | `.` |
| Opposite tap at d/4 to d/2 | `.` | `.-` | `.-` |
| Opposite tap at 1.25d to 1.75d | `.` | `.-` | `.-` |

Eight further cases per mode/speed cover single-contact release without a B
tail, inversion, SIDE DIT and repeated DAH, EXIT during mark/space, and a short
glitch. Total: **153 scenarios**. The tests check element order, mark-command
durations and start periods. In the host model, RF gate duration is nominal
mark plus 3 ms because the retained ramps are specified at half drive. Period
tolerance is 1.2 ms and gate-duration tolerance 0.4 ms. EXIT deliberately cuts
the mark short. Callback times are simulated, not hardware measurements.

`legacy_wait.c` contains only the attributed 0.5 `keyerWait`/`runPaddle` functions
from commit d0f30a67. AUTO carrier timelines are compared exactly with this
reference under identical mocked services. GPIO restoration and controls
1/5/7/8 are exercised directly.

## Persistence coverage

The second suite extracts the actual resident config load/save functions and
NOR write function. Erase/program operations act on a RAM Flash array. Slot 1
is the update target; slot 0, deliberately using the same visible app name,
must remain byte-identical. This tests slot isolation rather than assuming
that the visible name is a unique storage identifier.

Checks include first launch, AUTO/A/B normal-exit round trips, reboot with RAM
reset, unchanged settings, the 12-byte official 0.5 prefix, 0.2 migration,
downgrade compatibility, development-owner mismatch, 253 invalid mode values,
255 invalid extension tags, invalid base config and all setting controls.
Three toggles returning to the original mode must cause no NOR writes.

Five power-failure injections occur after sector erase and after 1/64/77/100
programmed bytes. A partially rewritten config may fall back to AUTO, and the
other slot stays unchanged. These are **failure characterization** checks:
the existing save is not atomic, and a damaged header may prevent the actual
loader from launching the app at all. Calling the config parser after a
simulated cut does not prove successful full firmware recovery.

## Limits

The suites do not emulate the entire resident loader or radio. Waveform tests
start with TX preparation already complete. Initial RX-to-TX latency, actual
scanner/interrupt timing, RF envelope, audio, electrical bounce, long-term NOR
endurance and physical power interruption require hardware checks. Passing
host tests does not constitute exhaustive radio validation.
