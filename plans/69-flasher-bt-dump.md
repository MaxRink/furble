# Web flasher Bluetooth debug dump capture

## Motivation

Once a user has flashed the debug firmware (plan 68), the diagnostic data still
lives behind a USB serial console that most reporters cannot reach without a
terminal and knowledge of the `bt` commands. This plan adds a browser Web Serial
panel to the flasher that connects to the debug console, drives the Bluetooth
diagnostic commands, and downloads a single capture file for the bug report. The
reporter clicks one button, reproduces the fault, clicks stop, and attaches the
downloaded dump.

This is PR-2 of a stacked pair and is design only in this document. It is not
implemented here.

## Dependencies

- Depends on PR-1 (plan 68), which publishes the debug firmware and manifests
  the reporter must flash first.
- Depends on fork PR #76 (`feat/64-bt-debug`), which adds the `bt` console
  command family. That PR must merge before this one. The command surface below
  is owned by #76; if #76 renames or reshapes a command, this panel follows.

## Design

### Console transport

The debug console is a USB CDC serial port at 115200 baud, 8N1. The panel uses
the Web Serial API (`navigator.serial.requestPort`, `port.open({ baudRate:
115200 })`). Web Serial is Chromium only and requires a user gesture and a
secure context, which the deployed HTTPS flasher satisfies. When Web Serial is
absent the panel hides itself and shows a one line note to use Chrome or Edge.

Console output is line based. Lines are colon delimited fields. The panel reads
with a `TextDecoderStream`, splits on newlines, and appends every raw line to
the capture buffer. It does not need to parse fields to capture them; parsing is
only for the small live status readout.

### Capture flow

The panel exposes a "Capture BT debug dump" button. On click it:

1. Opens the port and drains any banner.
2. Sends `bt journal on` to start recording the BT event journal.
3. Prompts the reporter to reproduce the problem, with a visible "Reproducing,
   click Stop when done" state.
4. On Stop, sends `bt journal dump` to emit the recorded journal, then
   `bt scan` for a current advertising snapshot, then `bt explore` for the GATT
   layout of the target. Each command's output is captured up to its prompt.
5. Closes the port and offers the capture buffer as a download,
   `furble-bt-dump-<timestamp>.txt`, via a Blob and an object URL.

Command completion is detected by the console prompt string, with a per command
timeout so a silent command cannot hang the capture. All sent commands and all
received lines are written to the buffer verbatim, so the download is a faithful
transcript.

### UI placement and style

The panel sits below the install button, collapsed by default, matching the
minimal page style. Plain English labels, no em-dashes, no framework. It reuses
the page's existing CSS. It never blocks the flasher: a browser without Web
Serial still flashes firmware normally.

## Files (for the implementing PR)

- `web-installer/index.html`: the Web Serial panel markup and its capture
  script.
- `web-installer/CLAUDE.md`: document the capture panel and the `bt` command
  sequence it drives.
- `plans/69-flasher-bt-dump.md`: mark implemented.

## Verification (for the implementing PR)

- Confirm the panel hides cleanly on a browser without Web Serial.
- With a debug build flashed on hardware, run a capture end to end and confirm
  the downloaded file contains the `bt journal dump`, `bt scan`, and
  `bt explore` output.
- Confirm a silent or missing command hits the timeout instead of hanging.
- Confirm the flasher still installs firmware with the panel present.

## Implementation state

Design only. Not implemented. Blocked on PR #76 (`feat/64-bt-debug`) and on
PR-1 (plan 68).
