# 127 - Development firmware revision identity

## Motivation

Development firmware previously displayed exactly the caller-provided `dev`
string. Two locally built images from different commits were indistinguishable
on the About page, companion BLE Device Information, and USB console. That
makes an OTA or bench result hard to tie back to the source that produced it.

## Implementation

- A PlatformIO pre-build adapter requests an unambiguous Git abbreviation of at
  least eight characters.
- An input version of `dev` becomes `dev+g<revision>`.
- A dirty checkout appends the deterministic `.dirty` marker. Ignored-only
  changes do not mark the build dirty.
- Release and explicitly named experimental versions remain byte-for-byte
  unchanged.
- Source archives without Git metadata retain the `dev` fallback instead of
  failing the build.
- Pull-request CI now requests `dev`, so the same resolver exercised by local
  hardware builds supplies its checked-out revision.

## Verification

- Pure host tests cover clean, dirty, ignored-only, release, and missing-Git
  behavior.
- Build one debug firmware with `FURBLE_VERSION=dev` and confirm both the
  compiler definition and USB `version` command contain the checked-out short
  revision.
- Release workflows continue to provide their explicit release version and do
  not receive a Git suffix.
