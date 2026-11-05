# Testing the CHIRP module against real CHIRP

Ad-hoc harnesses that stub the serial port are not enough. One passed every
check while the module failed on hardware with "Internal driver error" — the
fault was in driver registration, which a fake serial port never touches.

These run the module through CHIRP's own machinery instead.

## `run.sh` — CHIRP's driver test suite

```
tools/chirp-test/run.sh [path/to/chirp/checkout] [extra pytest args]
```

Builds a synthetic schema-2 image covering every modulation and both filter
settings, registers the module the way the GUI's "Load Module" does, and runs
`tests/test_drivers.py` against it. Cleans the image out of the CHIRP checkout
afterwards, including on failure.

Needs a CHIRP source checkout (default `~/code/chirp`) and `pytest`.

**Known pre-existing failure:** `TestCaseSettings::test_same_settings` fails
with `TypeError: __str__ returned non-string (type NoneType)` in the inherited
DTMF code handling. It fails identically on the module as it was before the
schema 2 work, so it is not a regression from it — verify against a known-good
revision before blaming a change.

## `test_clone_flow.py` — the download start-up path

```
python3 tools/chirp-test/test_clone_flow.py [path/to/module.py]
```

Replicates what `wxui/clone.py` does before it touches the port: load the
module, then call `detect_from_serial()` inside the same try/except. Fails if
anything but `NotImplementedError` comes back, or if detection touches the
port at all.

This is the regression test for two shipped bugs:

- a `detect_from_serial()` that probed the radio before the download and left
  it not answering, so every download died with "Header short read";
- removing that method while leaving a `@directory.detected_by` sub-model
  registered, which trips an assertion in `CloneModeRadio.detect_from_serial`
  and surfaces as "Internal driver error" before the port is opened.

The two go together. Do not add one without the other.

## `test_legacy_image.py` — the v2.0 CW storage conversion

```
python3 tools/chirp-test/test_legacy_image.py [path/to/module.py]
```

Loads a pre-v2.0 image (macros at `0x1C00`, CW settings at `0xA140`, schema at
`0xA170`) and checks it comes out in the v2.0 layout: macros at `0xD010` behind
a `CWM` header, CW settings at `0xA208`, a `CWX` header at `0xA200`, and the
channels that held macros emptied while a real channel in a failed block is
kept, and the key actions renumbered to fixed IDs. Also covers the armel `MIX`
guard (which skips the renumbering), a second load being a no-op, and the
upload gate refusing pre-v2.0 firmware.
