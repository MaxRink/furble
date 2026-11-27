import os
import sys
from pathlib import Path
from string import Template


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
  sys.path.insert(0, str(ROOT))

# The chip family and the flash offsets come from the shared layout module, so
# the generated manifest names exactly the family this PlatformIO environment
# targets and uses the bootloader offset that family boots from. See plan 372.
from tools.web_installer_layout import (  # noqa: E402  - after the sys.path fix
  bootloader_offset,
  chip_family_for_platform,
  PARTITION_TABLE_OFFSET,
  OTA_DATA_OFFSET,
  APP_OFFSET,
)


DEFAULT_REPOSITORY = "MaxRink/furble"
TEMPLATE = Path(__file__).resolve().with_name("manifest.tmpl")


def release_asset_base_url(version: str) -> str:
  repository = os.environ.get("REPOSITORY", DEFAULT_REPOSITORY)
  release = os.environ.get("RELEASE", version)
  return f"https://github.com/{repository}/releases/download/{release}"


def render(
  template: str, platform: str, version: str, asset_base_url: str = ""
) -> str:
  """Render one manifest for `platform`, a PlatformIO environment name."""
  if not asset_base_url:
    asset_base_url = release_asset_base_url(version)
  chip_family = chip_family_for_platform(platform)
  with open(template, "r", encoding="utf-8") as f:
    t = Template(f.read())
  return t.substitute(
    PLATFORM=platform,
    VERSION=version,
    ASSET_BASE_URL=asset_base_url.rstrip("/"),
    CHIP_FAMILY=chip_family,
    BOOTLOADER_OFFSET=bootloader_offset(chip_family),
    PARTITION_TABLE_OFFSET=PARTITION_TABLE_OFFSET,
    OTA_DATA_OFFSET=OTA_DATA_OFFSET,
    APP_OFFSET=APP_OFFSET,
  )


def generate(
  template: str, platform: str, version: str, asset_base_url: str = ""
):
  print(render(template, platform, version, asset_base_url))


if __name__ == "__main__":
  platform = os.environ["PLATFORM"]
  version = os.environ["VERSION"]
  asset_base_url = os.environ.get(
    "ASSET_BASE_URL", release_asset_base_url(version)
  )
  try:
    generate(str(TEMPLATE), platform, version, asset_base_url)
  except ValueError as error:
    raise SystemExit(f"generate-manifest: {error}") from None
