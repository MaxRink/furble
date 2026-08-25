# Contributing

Contributions to the `furble` project are welcome. This fork tracks
[gkoh/furble](https://github.com/gkoh/furble) and offers its changes upstream.
`CLAUDE.md` and `AGENTS.md` carry the detailed engineering guide; this file is
the short contributor workflow.

## Use of LLMs

`furble` is carefully and purposefully crafted and will remain so for the
forseeable future.
All tools must be wielded responsibly and precisely, large language models are
no exception.

To ensure provenance and traceability, please ensure:
- you review all changes
- you understand the origin of the changes
- you take full responsibility for the changes
All contributions will continue to pass through human review before acceptance.

## Building

furble is an ESP-IDF 5.x project built with PlatformIO. It is not Arduino.

There are five release board environments: `m5stick-c`, `m5stick-c-plus`,
`m5stick-s3`, `m5stack-core`, and `m5stack-core2`. Each has a matching `-debug`
environment that adds verbose logging and the USB serial console. CI and
releases build the five release environments only.

Every build needs the `FURBLE_VERSION` and `FURBLE_TEST` variables:

```sh
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug -t upload
```

A settings enum change must be applied consistently across all five committed
`sdkconfig.<env>` files, never for just one env. See `CLAUDE.md` for the build
traps.

## Testing

- Host tests compile the production camera code with plain clang or g++, no
  radio or ESP-IDF needed:

  ```sh
  cmake -S tests/host -B /tmp/furble-host-build
  cmake --build /tmp/furble-host-build
  ctest --test-dir /tmp/furble-host-build --output-on-failure
  ```

- The protocol and camera vector tests live under `tests/protocol` and
  `tests/camera`. CI runs them through `protocol-tests.yml` and
  `camera-tests.yml`.
- The host SDL simulator under `sim/` runs the real UI on a desktop. Build it
  with `sim/build.sh`. It drives scripted UI runs, the seeded UI fuzzer, and the
  documentation screenshot capture. It never changes firmware behavior.
- The USB serial console in debug builds is the automation surface for settings,
  GPS, shutter, and status. See `docs/console-commands.md`.
- Hardware verification happens before a PR. Only Fujifilm cameras are available
  for hardware tests. Other vendors are covered by code review and the FauxNY
  test camera, and are declared untested in the PR.

## Style

- Short declarative sentences. Plain English. No em-dashes anywhere: code
  comments, commits, PRs, and docs.
- 2-space indent. Match existing naming. clang-format 21 is enforced in CI; see
  `.clang-format`.

## Camera protocol changes

A new camera vendor or model needs its protocol data source cited in the PR:
a BLE HCI snoop log, a public protocol document, or an existing reference
implementation. State the hardware validation status explicitly.

## Plans

Each change is tracked by a numbered document under `plans/`. Update the
matching `plans/NN` doc in the same PR with the implementation state and any
deviation from the plan and its reason.

## Documentation

Docs must stay in sync with the code. Any PR that changes user-facing behavior,
settings, console commands, supported hardware, cameras, vendors, GPS units,
boards, or the UI must update the matching docs in the same PR. The doc surface
to change-type mapping is the "Documentation" section of `CLAUDE.md`. The wiki
is in-repo under `docs/wiki/`.

## Pull request checklist

- [ ] Builds clean for the affected board envs (`FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e <env>`).
- [ ] Host tests pass (`ctest --test-dir /tmp/furble-host-build`) where they apply.
- [ ] clang-format 21 clean. No em-dashes. Plain English.
- [ ] The matching `plans/NN` doc is updated with implementation state and deviations.
- [ ] Docs updated for this change (README / docs / wiki / walkthrough+screenshots as applicable).
- [ ] New camera or vendor work cites its protocol data source and states the hardware validation status.
- [ ] Hardware verified, or the untested surface is declared in the PR.

Reviewers confirm the mapped docs were updated for any user-facing, behavior, or
UI change before approving.
