from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))
from ble_gap_patch import apply_patch  # noqa: E402

Import("env")

framework_dir = env.PioPlatform().get_package_dir("framework-espidf")


def apply_ble_gap_patch():
  src_path = (
      Path(framework_dir)
      / "components"
      / "bt"
      / "host"
      / "nimble"
      / "nimble"
      / "nimble"
      / "host"
      / "src"
      / "ble_gap.c"
  )
  patch_path = Path(env.subst("$PROJECT_DIR")) / "patches" / "ble_gap.patch"
  apply_patch(src_path, patch_path)


# patch NimBLE
apply_ble_gap_patch()
