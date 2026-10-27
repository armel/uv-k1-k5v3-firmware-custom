# Validation record

## Identity and build

Reference: the user identifies `experiment/cw-qso-auto-repeat01` as validated
on UV-K5 V3. Its uncommitted working-tree source was frozen by SHA-256 rather
than inferred from an older branch commit. The source file is copied unchanged.
`release.json` records its exact hash and the approved application hashes.
The four frozen input files retain their byte representation through local
`.gitattributes`; other contribution text uses LF.

Contribution base: official `main`, commit
`3574e942e0d69bc2cb59eb913b8d4744a7739113` (checked 2026-10-27).
Native GNU Arm GCC **13.3.1 20240614 / 13.3.Rel1**, Cortex-M0+, `-Os`, ordinary
4 KiB linker and `-Werror`: the rebuilt APP is **byte-identical** to the approved
file. The repository Dockerfiles also select 13.3.Rel1; a Docker build was not
executed in this session.

| Item | Result |
| --- | ---: |
| Overlay, BSS included | 4,092 / 4,096 B |
| BSS | 92 B |
| Assets | 2,282 B |
| Overlay margin | 4 B |
| Complete FAP1 file | 6,438 B |
| APP SHA-256 | `9aa7749f6614a3c7ef111edf78582a70fc18ee491441602d8fd3df889018517a` |
| Executable-section SHA-256 | `d5ec353509642b5aeba50386102e5d813ece1de4cbd9364e492bc1c07bb9e3c4` |

## Software checks rerun for this package

| Suite | Observed result |
| --- | --- |
| Functional / five-message Morse oracles, 5–60 WPM | 1,438 checks passed |
| Persisted configuration | 209,680 checks passed |
| Startup and held controls | 317 checks passed |
| AUTO / IAMB A / IAMB B | 144 scenarios passed |
| Bank boundaries, mutations and tokens | 1,488 checks passed |
| 16-message navigation and PTT | 1,184 checks, including 896 speed/message oracles, passed |
| CW SETTINGS | 2,029 checks passed |
| Content preview / MENU hint | 1,719 / 822 checks passed |
| Radio screen rendering | 36 screens passed |
| SIDE repetition and stop | 127 checks passed, including 5/18/35/60 WPM |
| Linked Thumb argument/ABI checks | 157 checks passed |
| HP/Tone1/RF commands and RX restoration | 4 targeted traces passed |
| Editor core | 19 groups passed; 6,438 individual-byte corruptions refused; all 16 records edited independently; 64 record-header tampering cases refused |
| Editor/browser | Edge headless on `file://`, network forcibly offline, zero external page requests; bootstrap, default download, personalization, reimport, errors and reset passed |
| Production reader of editor-generated files | 154 checks on two personalized files passed |
| Independent packer/CRC | Custom output repacks identically with the existing repository packer; executable bytes preserved |

The radio suites are the existing ones. Assertions for the deliberately removed
SIDE 15/16 shortcuts were replaced by the repetition tests; message 15/16
navigation and transmission remain covered. Test counts are not summed as
independent hardware trials.

In the host model the inter-message RF gap is **3,003,422–3,003,448 µs**. The
next TX preparation starts no earlier than three seconds after the final edge;
the normal preparation and foreground cadence account for the extra fraction.
These are simulated command times, not oscilloscope measurements. The Thumb
test executes the actual linked wrapper encodings up to API dispatch/return;
it is not a complete MCU/RF emulator. Tone1 at zero frequency does not prove
analog silence or absence of clicks.

The separate approved APP and the embedded base64 decode are exactly equal.
Generating without edits returns that same file. Personalized files preserve
the executable, code CRC, non-message assets and record headers; only message
payloads and the asset CRC may differ. Both direct checks and the real browser
download/reimport exercise this. The browser also changes the example callsign
and name, checks 64/65-token boundaries, and blocks invalid Unicode and files.
The three legacy approved profiles were also imported and round-tripped
unchanged using the existing packaging-workspace fixtures; historical binaries
are not included in this contribution.

## Reproduce

Use a complete firmware repository checkout, Python 3, host GCC, Node.js 18+
and the pinned ARM toolchain. Set `ARM_CC` and, if needed, `HOST_CC`, `NODE` or
`EDGE_EXE` to local executables. Build before running tests:

```sh
python3 App/apps/cwqsoauto/build.py
python3 App/apps/cwqsoauto/test/run.py
python3 App/apps/cwqsoauto/test/run.py --browser
```

The first run omits the browser. The second includes a real local browser and
checks both its personalized download and the core-generated sample through
the production message reader. For editor-only verification:

```sh
node App/apps/cwqsoauto/test/editor-core.cjs
node App/apps/cwqsoauto/test/editor-browser.cjs
```

Outputs go under ignored `test/out*` directories. No historical executable,
development clone, intermediate ELF/BIN/MAP, browser profile or temporary log
is part of the contribution. `waveform-reference.json` is the retained test
oracle, not a generated debug dump.

## Hardware report and remaining checks

The user reports the repeat01 version validated on a real **UV-K5 V3**, and
previously confirmed the early-HP first-DIT correction. The package preserves
that application byte-for-byte. A very light switching click was reported
during development; its level/frequency on this artifact was not measured here.

No physical radio, RF envelope capture, acoustic measurement, UV Studio serial
installation or fresh firmware-main hardware qualification was performed in
this packaging session. WPM/mode coverage above is software coverage, not a
claim of hardware trials at every speed. UV-K1, other compiler versions and
other browsers have not been qualified here. Catalogue publication itself
cannot be tested before the maintainer publishes the contribution.

Suggested maintainer smoke test: personalize callsign/name, install the local
APP with UV Studio, compare the first DIT after RX to the validated version,
let SIDE 1 send twice, stop with SIDE 2 during a mark and then during a pause,
and check PTT/navigation, manual keying and RX audio. Keep the customized file
as the backup; the editor does not recover messages directly from the radio.
