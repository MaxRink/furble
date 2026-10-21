# Dependabot updates

## Motivation

The fork needs reviewed updates for its supported dependency manifests, including CI and companion tooling.

## Design

Dependabot checks GitHub Actions, Python tooling, Android Gradle dependencies, and the Apple Swift package weekly. Routine minor and patch changes are grouped for review. Firmware pins in PlatformIO and ESP-IDF component manifests remain manual; Dependabot does not support those formats. Updates are proposed through pull requests and do not deploy or flash hardware.

## Implementation state

The configuration and contributor guidance are included in this change. The YAML schema and manifest coverage checks passed. No package versions or firmware code change. The owner authorized configuration validation in place of full application and hardware suites for this configuration-only update.

## Deviations

PlatformIO and ESP-IDF pins remain manual because they are not supported by Dependabot. This fork-only maintenance change has no upstream PR.
