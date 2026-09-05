#!/usr/bin/env python3
"""Minimal stdlib PNG reader plus a pixel diff for the UI audit."""
import struct, zlib, sys

def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', path
    pos, idat, plte, trns = 8, b'', None, None
    w = h = bd = ct = None
    while pos < len(data):
        ln, = struct.unpack('>I', data[pos:pos+4])
        typ = data[pos+4:pos+8]
        chunk = data[pos+8:pos+8+ln]
        if typ == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', chunk[:10])
        elif typ == b'IDAT':
            idat += chunk
        elif typ == b'PLTE':
            plte = chunk
        elif typ == b'tRNS':
            trns = chunk
        elif typ == b'IEND':
            break
        pos += 12 + ln
    raw = zlib.decompress(idat)
    ch = {0:1, 2:3, 3:1, 4:2, 6:4}[ct]
    assert bd == 8, (path, bd)
    bpp = ch
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p+stride]); p += stride
        if f == 1:
            for i in range(bpp, stride): line[i] = (line[i] + line[i-bpp]) & 255
        elif f == 2:
            for i in range(stride): line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i-bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i-bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i-bpp] if i >= bpp else 0
                pa, pb, pc = abs(b-c), abs(a-c), abs(a+b-2*c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y*stride:(y+1)*stride] = line
        prev = line
    # normalise to RGB tuples
    px = []
    for y in range(h):
        row = []
        for x in range(w):
            i = y*stride + x*bpp
            if ct == 3:
                idx = out[i]
                row.append((plte[idx*3], plte[idx*3+1], plte[idx*3+2]))
            elif ct == 2:
                row.append((out[i], out[i+1], out[i+2]))
            elif ct == 6:
                row.append((out[i], out[i+1], out[i+2]))
            elif ct == 0:
                row.append((out[i],)*3)
            else:
                row.append((out[i],)*3)
        px.append(row)
    return w, h, px

def diff(a, b):
    wa, ha, pa = read_png(a)
    wb, hb, pb = read_png(b)
    if (wa, ha) != (wb, hb):
        return dict(same=False, size_changed=True, a=(wa,ha), b=(wb,hb))
    n = 0
    minx, miny, maxx, maxy = wa, ha, -1, -1
    rows = [0]*ha
    for y in range(ha):
        ra, rb = pa[y], pb[y]
        for x in range(wa):
            if ra[x] != rb[x]:
                n += 1; rows[y] += 1
                if x < minx: minx = x
                if x > maxx: maxx = x
                if y < miny: miny = y
                if y > maxy: maxy = y
    total = wa*ha
    return dict(same=(n==0), pixels=n, pct=100.0*n/total, w=wa, h=ha,
                box=(minx, miny, maxx, maxy) if n else None, rows=rows)

if __name__ == '__main__':
    import json
    print(json.dumps(diff(sys.argv[1], sys.argv[2])))

# --- text preview helpers, appended for the audit ---
def preview(path, cols=60):
    """Coarse ASCII render so a PNG can be inspected as text."""
    w, h, px = read_png(path)
    step = max(1, w // cols)
    ramp = " .:-=+*#%@"
    lines = []
    for y in range(0, h, step * 2):
        row = ""
        for x in range(0, w, step):
            n = 0; s = 0
            for yy in range(y, min(y + step * 2, h)):
                for xx in range(x, min(x + step, w)):
                    r, g, b = px[yy][xx]
                    s += (r * 299 + g * 587 + b * 114) // 1000
                    n += 1
            lum = s // max(n, 1)
            row += ramp[(255 - lum) * (len(ramp) - 1) // 255]
        lines.append(row)
    return "\n".join(lines)

def rowmap(a, b):
    """Per-row changed-pixel counts, compressed to runs."""
    d = diff(a, b)
    if d.get("same") or d.get("size_changed"):
        return d
    runs = []
    prev = None
    for y, c in enumerate(d["rows"]):
        hot = c > 0
        if hot != prev:
            runs.append([y, y, c])
            prev = hot
        else:
            runs[-1][1] = y
            runs[-1][2] = max(runs[-1][2], c)
    d["runs"] = [r for r in runs if r[2] > 0]
    del d["rows"]
    return d
