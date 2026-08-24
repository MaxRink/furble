# CI action lock

Every GitHub Action reference in `.github/workflows/` is pinned to an
immutable commit SHA. The release tag corresponding to each SHA is recorded
in [`tools/ci-action-lock.json`](../tools/ci-action-lock.json). The inline
workflow comments retain the human-readable release version for maintenance.

The SHAs were checked against the official tags in each action repository.
The Android emulator runner uses the current `v2.38.0` commit rather than the
floating `v2` alias. The github-script SHA is the commit shared by its `v7`
and `v7.1.0` tags. The softprops action uses the dereferenced
`v3.0.2` commit rather than the annotated major tag object.

This file is limited to CI action provenance. Firmware, Android dependency,
and SDK locks belong to separate independently mergeable changes.
