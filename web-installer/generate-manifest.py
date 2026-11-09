import json
import os
from string import Template


DEFAULT_REPOSITORY = "MaxRink/furble"


def release_asset_base_url(version: str) -> str:
  repository = os.environ.get("REPOSITORY", DEFAULT_REPOSITORY)
  release = os.environ.get("RELEASE", version)
  return f"https://github.com/{repository}/releases/download/{release}"


def render(
  template: str, platform: str, version: str, asset_base_url: str = "",
  ota_data: bool = True
) -> str:
  if not asset_base_url:
    asset_base_url = release_asset_base_url(version)
  with open(template, "r", encoding="utf-8") as f:
    text = Template(f.read()).substitute(
      PLATFORM=platform,
      VERSION=version,
      ASSET_BASE_URL=asset_base_url.rstrip("/"),
    )
  if ota_data:
    return text
  # A single factory image (the StickC debug profiles) has no otadata
  # partition and no ota_data_initial.bin, so its manifest must not flash one.
  manifest = json.loads(text)
  for build in manifest["builds"]:
    build["parts"] = [
      part for part in build["parts"] if "ota_data_initial" not in part["path"]
    ]
  return json.dumps(manifest, indent=2)


def generate(
  template: str, platform: str, version: str, asset_base_url: str = "",
  ota_data: bool = True
):
  print(render(template, platform, version, asset_base_url, ota_data))


if __name__ == "__main__":
  template = "manifest.tmpl"
  platform = os.environ["PLATFORM"]
  version = os.environ["VERSION"]
  asset_base_url = os.environ.get(
    "ASSET_BASE_URL", release_asset_base_url(version)
  )
  generate(template, platform, version, asset_base_url,
           os.environ.get("OTA_DATA", "1") != "0")
