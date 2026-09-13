# 178 - simulator startup settings order

## Motivation

The firmware initializes settings before the platform because
`FurblePlatform.cpp` reads `IMU` and `FB_OUTPUT` while constructing the
`M5.begin()` configuration. The host simulator initialized its platform first,
then wrote scenario settings, so it had no matching boot-input snapshot
boundary. The SDL platform still deliberately forces its host IMU and speaker
flags off. This change aligns the observable input boundary without claiming
physical M5 configuration parity.

## Change

`sim/main.cpp` now initializes NVS settings and applies parsed scenario values
before `Platform::init()`. The existing process order remains unchanged:
`preparePreferences()`, watchdog start, SDL setup, and the simulator thread
handoff stay in that order. `panelReady` is still published only after platform
initialization, so the SDL monitor-list race guard is unchanged.

The profiler call remains after platform initialization and panel publication.
Its call placement is unchanged, but moving settings initialization before it
does change which earlier work is outside the measured window. The profile
still does not include initial platform power configuration. That is a separate
measurement decision.

This patch does not change Companion rig behavior, add production Companion
GATT startup to the simulator, add `TimeKeeper::init()`, or model physical IMU,
speaker, PMIC, or RF behavior.

## Regression coverage

`tests/test_sim_startup_order.py` is a dependency-free Python source contract.
It checks that settings and scenario application precede platform construction,
that the panel readiness and profiler boundaries remain after platform, and
that the platform observation captures the actual `IMU` and `FB_OUTPUT` loads.
The Python source contract is included by the existing unittest discovery job
(`python -m unittest discover -s tests -p 'test_*.py'`). The
boot-splash-disabled scenario is a separate runtime check selected through the
scenario manifest and run by `sim/scripts/run-e2e.sh`; it seeds both values and
asserts the recorded snapshot.

## Verification

The delegated implementation lane did not run builds, tests, hardware checks,
or GitHub operations. The root validation lane should run the existing Python
unittest suite, simulator build, certified end-to-end scenarios, watchdog
scenarios, and environment-order guard. The focused test command is:

```text
python3 -m unittest tests.test_sim_startup_order
```

The simulator scenario suite must continue to prove UI and settings behavior.
It must not inject BLE advertisements or scan callbacks during idle boot.

## Validation evidence

Root validation at commit `08f5dbfaf43c367db672b99ad368b9163fd6e0ae` passed the
M5StickS3 simulator build and the `boot-splash-disabled` runtime scenario. The
scenario observed `boot_settings_imu=1`, `boot_settings_fb_output=1`, and
`ui.page=main`. The remaining serialized checks are still pending.

## Implementation state

Implemented on the MaxRink/furble fork master base
`eca30b5bd7c4ee005a7fec0997f151279f13bb2f` in the bounded settings-order
slice. The commit and bundle are handed to the root agent for serialized
validation.
