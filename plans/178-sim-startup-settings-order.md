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
or GitHub operations. Root ran the existing Python unittest suite, simulator
build, certified end-to-end scenarios, watchdog scenarios, and environment
ordering guard. The focused test command remains:

```text
python3 -m unittest tests.test_sim_startup_order
```

The simulator scenario suite must continue to prove UI and settings behavior.
It must not inject BLE advertisements or scan callbacks during idle boot.

## Validation evidence

Root validation at commit `08f5dbfaf43c367db672b99ad368b9163fd6e0ae` passed the
M5StickS3 simulator build and the `boot-splash-disabled` runtime scenario. The
scenario observed `boot_settings_imu=1`, `boot_settings_fb_output=1`, and
`ui.page=main`.

The restored `1b7a47a2904fdc70adcf766859e309da69f7ad55` tip then passed Python
unittest discovery (179 tests in 2.029 s), the manifest gate, a clean rebuild,
all five S3 watchdog scenarios, and the S3 touch end-to-end gate (123
scenarios). Reverting the startup order failed as expected with
`boot_settings_imu expected 1 got 0`; restoring the source rebuilt and passed
the positive boot check.

The full environment-order guard also passed with exit 0. The current binary
passed both guarded runs, while the pre-fix S3 binary
`/home/a92615428/wt/p65/sim/build/furble-sim` (SHA-256
`2d82bb0fcd3a7ba57b8d7472a2857335fcc008e9f4735e54bfea501ffd6e64a0`) was
rejected with status 86 for both `FURBLE_SIM_PREFS` and
`FURBLE_SIM_RESTART_STEP`. The captured root log is
`~/b/startup-final-env-order.log`.

## Implementation state

Implemented on the MaxRink/furble fork master base
`eca30b5bd7c4ee005a7fec0997f151279f13bb2f` in the bounded settings-order
slice. The commit and bundle are handed to the root agent for serialized
validation.
