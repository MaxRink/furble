# Icons

Icons are sourced from [Material Symbols](https://fonts.google.com/icons),
licensed under Apache-2.0.

Icons were configured for download as:
- No fill
- Weight 300
- Grade 0
- Size 24px

Source SVGs for icons added to this repo are kept under `svg/` so the arrays
below can be regenerated. Furble recolours the black glyph to the theme colour
at draw time, so the source only needs the black-on-transparent glyph.

They are further converted to the correct sized PNG by:
```
inkscape -w 48 -h 48 icon.svg -o icon.png
```
Menu icons are 48x48; the `_24` variants used on the small StickC screens are
the same SVG rendered at 24x24. If inkscape is not available, rsvg-convert
produces an equivalent PNG:
```
rsvg-convert -w 48 -h 48 icon.svg -o icon.png
```

Then converted to a compressed in-memory variable by:
```
LVGLImage.py --ofmt C --cf RGB565A8 --compress LZ4 icon.png
```

New arrays are formatted with clang-format to match the rest of the tree.
