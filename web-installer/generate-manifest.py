import os
from string import Template


DEFAULT_REPOSITORY = "MaxRink/furble"


def release_asset_base_url(version: str) -> str:
  repository = os.environ.get("REPOSITORY", DEFAULT_REPOSITORY)
  release = os.environ.get("RELEASE", version)
  return f"https://github.com/{repository}/releases/download/{release}"


def generate(
  template: str, platform: str, version: str, asset_base_url: str = ""
):
  if not asset_base_url:
    asset_base_url = release_asset_base_url(version)
  with open(template, "r", encoding="utf-8") as f:
    t = Template(f.read())
    print(
      t.substitute(
        PLATFORM=platform,
        VERSION=version,
        ASSET_BASE_URL=asset_base_url.rstrip("/"),
      )
    )


if __name__ == "__main__":
  template = "manifest.tmpl"
  platform = os.environ["PLATFORM"]
  version = os.environ["VERSION"]
  asset_base_url = os.environ.get(
    "ASSET_BASE_URL", release_asset_base_url(version)
  )
  generate(template, platform, version, asset_base_url)
