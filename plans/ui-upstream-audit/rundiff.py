#!/usr/bin/env python3
"""Three-way page diff over the audit capture matrix."""
import os, sys, json
sys.path.insert(0, os.path.expanduser("~/b/audit"))
import pngdiff

ROOT = os.path.expanduser("~/b/audit/shots")
PANELS = ["135x240", "80x160", "320x240"]
SIZES = ["small", "normal", "large"]
# comparable layout directories: (upui/master dir, 273 dir)
LAYOUTS = [("buttons", "buttons-lg0"), ("touch", "touch")]

def pages(d):
    if not os.path.isdir(d):
        return set()
    return {f[:-4] for f in os.listdir(d) if f.endswith(".png")}

rows = []
for panel in PANELS:
    for lay_a, lay_b in LAYOUTS:
        for size in SIZES:
            du = f"{ROOT}/upui/{panel}/{lay_a}/{size}"
            dm = f"{ROOT}/master/{panel}/{lay_a}/{size}"
            d3 = f"{ROOT}/273/{panel}/{lay_b}/{size}"
            pu, pm, p3 = pages(du), pages(dm), pages(d3)
            for page in sorted(pm | pu | p3):
                rec = dict(panel=panel, layout=lay_a, size=size, page=page)
                for tag, x, y, dx, dy in [
                    ("up_master", pu, pm, du, dm),
                    ("master_273", pm, p3, dm, d3),
                    ("up_273", pu, p3, du, d3),
                ]:
                    if page not in x or page not in y:
                        rec[tag] = "absent" if page not in x else "n/a"
                        continue
                    rec[tag] = pngdiff.rowmap(f"{dx}/{page}.png", f"{dy}/{page}.png")
                rows.append(rec)
json.dump(rows, open(os.path.expanduser("~/b/audit/diffs.json"), "w"))
print("pairs:", len(rows))
