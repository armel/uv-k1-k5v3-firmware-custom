# DTMF Monitor

Receive-only DTMF monitor for BK4819 radios, overlay ABI 1 / API 2.
Select an FM channel or frequency before launching the app from the Apps menu.
The app stays on the selected RX VFO; it does not scan or transmit.

## Display and controls

- Receives `0–9`, `A–D`, `*` and `#`. A gap of at least one second starts a new sequence.
- Keeps eight sequences in RAM, up to 32 characters each. `+` marks truncation;
  the first 32 characters are retained. Records are discarded on exit.
- Navigation keys browse older/newer sequences while reception continues.
  MENU returns to the latest sequence.
- `0` toggles speaker monitoring (off on launch). The status-bar speaker icon
  indicates monitoring is enabled. Audio follows the selected channel's hardware
  squelch thresholds and delays; SQL 0 leaves audio open. CTCSS/DCS conditions
  are not applied to the speaker gate. DTMF detection continues independently.
- Press `*` twice within two seconds to clear all records.
- EXIT or PTT exits. PTT does not transmit inside the app.

The status bar shows the app name, WAIT when empty, speaker state, history
navigation arrows and battery level. The selected frequency appears below the
records, with compact key hints at the bottom. LIVE means the latest sequence
is still within its one-second grouping window; DONE means that window ended.
Neither label indicates current audio activity.

Characters come from the BK4819 detector. Each hardware event is retained,
including repeated characters; repeated events during a held tone depend on
the chip. Decoded characters are not authenticated sender identities.

## Build and test

From the repository root:

```sh
./compile-app.sh dtmfmonitor
App/apps/dtmfmonitor/tests/run.sh
```

The build produces `build/Apps/DTMFMonitor.app`, with no assets. Code, constants,
data and BSS must fit the 4 KiB overlay workspace; the linker enforces this limit.

Host tests use AddressSanitizer and UndefinedBehaviorSanitizer. They cover digit
mapping, sequence boundaries, timer rollover, repeated digits, truncation,
history, clear confirmation, simulated IRQs, speaker squelch gating and register
restoration. Simulated events do not establish RF decoding accuracy.
