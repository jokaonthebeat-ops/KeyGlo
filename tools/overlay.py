#!/usr/bin/env python3
"""Overlay QA: blend the uishot render 50% with the approved reference.

    python3 tools/overlay.py build/KeyGlo-ui.png [out.png]

Also writes a difference heatmap (out-diff.png): red where the two images
disagree strongly, so panel drift shows up as red edges.

Stdlib-only PNG codec - this machine has no PIL.
"""
import struct, sys, zlib


def read_png_rgba(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', path
    pos, w, h, idat, ctype = 8, None, None, b'', None
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]
        typ = d[pos+4:pos+8]
        data = d[pos+8:pos+8+ln]
        if typ == b'IHDR':
            w, h, bd, ctype = struct.unpack('>IIBB', data[:10])
        elif typ == b'IDAT':
            idat += data
        pos += 12 + ln
    raw = zlib.decompress(idat)
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    stride = w * ch
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos+stride]); pos += stride
        if f == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i-ch]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i-ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i-ch] if i >= ch else 0
                b = prev[i]
                c = prev[i-ch] if i >= ch else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        prev = line
        base = y * w * 4
        if ch == 4:
            out[base:base + w*4] = line
        elif ch == 3:
            for x in range(w):
                out[base+x*4:base+x*4+3] = line[x*3:x*3+3]
                out[base+x*4+3] = 255
        elif ch == 1:
            for x in range(w):
                g = line[x]
                out[base+x*4:base+x*4+4] = bytes((g, g, g, 255))
        elif ch == 2:
            for x in range(w):
                g, a = line[x*2], line[x*2+1]
                out[base+x*4:base+x*4+4] = bytes((g, g, g, a))
    return w, h, out


def write_png(path, w, h, px):
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\x00' + bytes(px[y*w*4:(y+1)*w*4]) for y in range(h))
    open(path, 'wb').write(
        b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(raw, 6))
        + chunk(b'IEND', b''))


def main():
    shot_path = sys.argv[1] if len(sys.argv) > 1 else 'build/KeyGlo-ui.png'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'build/KeyGlo-overlay.png'
    ref_path = 'Spec/00_REFERENCE/KeyGlo_Approved_UI_1491x1055.png'

    rw, rh, ref = read_png_rgba(ref_path)
    sw, sh, shot = read_png_rgba(shot_path)
    if (rw, rh) != (sw, sh):
        print(f'size mismatch: ref {rw}x{rh} vs shot {sw}x{sh}')
        sys.exit(1)

    blend = bytearray(len(ref))
    diff = bytearray(len(ref))
    hot = 0
    for i in range(0, len(ref), 4):
        dr = abs(ref[i] - shot[i])
        dg = abs(ref[i+1] - shot[i+1])
        db = abs(ref[i+2] - shot[i+2])
        d = (dr + dg + db) // 3
        blend[i]   = (ref[i] + shot[i]) >> 1
        blend[i+1] = (ref[i+1] + shot[i+1]) >> 1
        blend[i+2] = (ref[i+2] + shot[i+2]) >> 1
        blend[i+3] = 255
        if d > 48:
            diff[i], diff[i+1], diff[i+2] = 255, 40, 60
            hot += 1
        else:
            g = (ref[i] + ref[i+1] + ref[i+2]) // 6
            diff[i] = diff[i+1] = diff[i+2] = g
        diff[i+3] = 255

    write_png(out_path, rw, rh, blend)
    diff_path = out_path.replace('.png', '-diff.png')
    write_png(diff_path, rw, rh, diff)
    pct = 100.0 * hot / (rw * rh)
    print(f'{out_path}  and  {diff_path}')
    print(f'strong-difference pixels: {pct:.2f}%')


if __name__ == '__main__':
    main()
