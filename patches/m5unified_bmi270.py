"""Compile the Bosch BMI270 out of a board that has no BMI270 (post: lib_deps).

`m5::bmi270_config_file` is an 8,192-byte initialisation blob that M5Unified
links whenever its IMU probe is compiled, and `--gc-sections` cannot drop it
because the probe references the array. On an image pinned to a board whose IMU
is an MPU6886, MPU9250 or SH200Q, that probe can never succeed, so the blob and
the driver behind it are dead weight in the flash slot.

`platformio.ini` passes `-DFURBLE_IMU_BMI270=0` on those environments and
`=1` on the two that carry the part, and the two patches read that macro:

- `m5unified_bmi270_blob.patch` guards the driver translation unit, which is
  where the blob is defined.
- `m5unified_bmi270_probe.patch` guards the include and the probe block in
  `IMU_Class.cpp`, so nothing references the class the first patch drops.

Both default the macro to 1, so a tree built without the define behaves exactly
as upstream and a board that carries a BMI270 cannot lose it by omission.
`patches/test_m5unified_bmi270.py` checks the default and the guard sites.
"""

from pathlib import Path
import sys

Import("env")

patches_dir = Path(env.subst("$PROJECT_DIR")) / "patches"
sys.path.insert(0, str(patches_dir))
from ble_gap_patch import apply_patch  # noqa: E402

BLOB_MARKER = "furble: a board with no Bosch BMI270 never runs this probe"
PROBE_MARKER = "furble: the probe and its 8 KiB blob only exist"

utility = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "M5Unified" / "src" / "utility"

apply_patch(
    utility / "imu" / "BMI270_Class.cpp",
    patches_dir / "m5unified_bmi270_blob.patch",
    marker=BLOB_MARKER,
)
apply_patch(
    utility / "IMU_Class.cpp",
    patches_dir / "m5unified_bmi270_probe.patch",
    marker=PROBE_MARKER,
)
