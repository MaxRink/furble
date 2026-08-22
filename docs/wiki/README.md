# Wiki pages

These files are the GitHub wiki pages for this fork, prepared here because the
wiki repository (`MaxRink/furble.wiki.git`) did not exist yet at the time of
writing. A GitHub wiki repository is only created after the first page is added
through the web UI.

To publish them:

1. Create the first wiki page once through the repository Wiki tab so GitHub
   initializes `MaxRink/furble.wiki.git`.
2. Clone that wiki repository.
3. Copy these files into it, keeping `img/` alongside the pages.
4. Commit and push.

Pages:

- `Home.md`: overview and links.
- `Getting-Started.md`: boards, flashing, first pairing.
- `UI-Walkthrough.md`: a screenshot of every page.
- `Settings-Reference.md`: every setting.
- `Controls.md`: button map and input modes.
- `Supported-Hardware.md`: controllers, cameras, and GPS units.
- `Console-Commands.md`: the USB serial console in debug builds.
- `img/`: the screenshots the pages embed.

The page content mirrors the in-repo docs under `docs/`. Internal links use wiki
page names (no `.md`) rather than file paths.
