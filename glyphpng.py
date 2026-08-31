#!/usr/bin/env python3
"""Turn letters into a square picture - a profile photo, logo, or chat icon.

Run it with no arguments and it will ask you what you want, step by step.
Or say it directly:

    ./glyphpng.py A
    ./glyphpng.py "JD" --style modern --colors crimson
    ./glyphpng.py N --text-color gold --background none

Pictures are saved into the "pictures" folder as PNG files, at full quality.

Under the hood this reads the font files itself and draws every curve by
hand - no image or font libraries are used, only Python's own zlib for the
compression the PNG format requires.
"""
import argparse
import json
import math
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
FONTDIR = os.path.join(HERE, "assets", "fonts")

# Curated picks from assets/fonts. Left is the short name you pass to --font.
STYLES = [
    # key            what you see in the menu        the file behind it
    ("classic",      "Classic serif",                "newcomputermodern/NewCM10-Regular.otf"),
    ("classic-bold", "Classic serif, bold",          "newcomputermodern/NewCM10-Bold.otf"),
    ("italic",       "Elegant italic",               "newcomputermodern/NewCM10-Italic.otf"),
    ("modern",       "Clean modern sans",            "newcomputermodern/NewCMSans10-Bold.otf"),
    ("typewriter",   "Typewriter",                   "newcomputermodern/NewCMMono10-Bold.otf"),
    ("medieval",     "Medieval / uncial",            "newcomputermodern/NewCMUncial10-Bold.otf"),
    ("chinese",      "Chinese, heavy block",         "fandol/FandolHei-Bold.otf"),
    ("chinese-song", "Chinese, classic printed",     "fandol/FandolSong-Bold.otf"),
    ("chinese-brush","Chinese, brush written",       "fandol/FandolKai-Regular.otf"),
    ("japanese",     "Japanese, extra heavy",        "haranoaji/HaranoAjiGothic-Heavy.otf"),
    ("japanese-min", "Japanese, classic printed",    "haranoaji/HaranoAjiMincho-Bold.otf"),
    ("poster",       "Poster sans, very heavy",      "unfonts-core/UnGraphicBold.ttf"),
    ("korean",       "Korean, classic printed",      "unfonts-core/UnBatangBold.ttf"),
    ("korean-round", "Korean, rounded",              "unfonts-core/UnDotumBold.ttf"),
    ("brush",        "Brush calligraphy",            "unfonts-core/UnGungseo.ttf"),
    ("handwritten",  "Handwritten",                  "unfonts-core/UnPilgiBold.ttf"),
    ("terminus",     "Terminus bitmap, 24 px",       "terminus/ter-u24n.otb"),
    ("terminus-bold","Terminus bitmap, bold",        "terminus/ter-u24b.otb"),
    ("icons",        "Icon symbols",                 "icons/matrix-icons.otf"),
]
STYLE_FILE = {k: v for k, _, v in STYLES}
STYLE_LABEL = {k: l for k, l, _ in STYLES}
DEFAULT_STYLE = "medieval"

# ---------------------------------------------------------------- sfnt/tables

class Reader:
    """Big-endian cursor over a bytes buffer."""

    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def seek(self, p):
        self.p = p

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def s8(self):
        v = self.u8()
        return v - 256 if v >= 128 else v

    def u16(self):
        v = struct.unpack_from(">H", self.d, self.p)[0]
        self.p += 2
        return v

    def s16(self):
        v = struct.unpack_from(">h", self.d, self.p)[0]
        self.p += 2
        return v

    def u32(self):
        v = struct.unpack_from(">I", self.d, self.p)[0]
        self.p += 4
        return v

    def f2dot14(self):
        return self.s16() / 16384.0


class Font:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.data = fh.read()
        r = Reader(self.data)
        tag = r.u32()
        if tag == 0x74746366:  # 'ttcf' - take the first face
            r.u32()
            r.u32()
            r.seek(r.u32())
            r.u32()
        num = (r.u16(), r.u16(), r.u16(), r.u16())[0]
        self.tables = {}
        for _ in range(num):
            name = self.data[r.p:r.p + 4].decode("latin-1")
            r.p += 4
            r.u32()
            off, length = r.u32(), r.u32()
            self.tables[name] = (off, length)

        self.units_per_em = 1000
        if "head" in self.tables:
            h = Reader(self.data, self.tables["head"][0])
            h.seek(h.p + 18)
            self.units_per_em = h.u16()
            h.seek(self.tables["head"][0] + 50)
            self.index_to_loc = h.s16()

        self.num_glyphs = 0
        if "maxp" in self.tables:
            m = Reader(self.data, self.tables["maxp"][0] + 4)
            self.num_glyphs = m.u16()

        self._read_hmtx()
        self._read_cmap()
        self.cff = None
        self.loca = None
        if "CFF " in self.tables:
            self.cff = CFF(self.data, self.tables["CFF "][0])
        elif "loca" in self.tables:
            self._read_loca()

    def _read_hmtx(self):
        self.advances = []
        if "hhea" not in self.tables or "hmtx" not in self.tables:
            return
        hh = Reader(self.data, self.tables["hhea"][0] + 34)
        n = hh.u16()
        r = Reader(self.data, self.tables["hmtx"][0])
        adv = 0
        for _ in range(n):
            adv = r.u16()
            r.s16()
            self.advances.append(adv)
        self.last_advance = adv

    def advance(self, gid):
        if not self.advances:
            return self.units_per_em // 2
        if gid < len(self.advances):
            return self.advances[gid]
        return self.last_advance

    def _read_loca(self):
        off, _ = self.tables["loca"]
        r = Reader(self.data, off)
        n = self.num_glyphs + 1
        if self.index_to_loc == 0:
            self.loca = [r.u16() * 2 for _ in range(n)]
        else:
            self.loca = [r.u32() for _ in range(n)]

    def _read_cmap(self):
        self.cmap = {}
        if "cmap" not in self.tables:
            return
        base = self.tables["cmap"][0]
        r = Reader(self.data, base + 2)
        n = r.u16()
        best, best_score = None, -1
        for _ in range(n):
            pid, eid, off = r.u16(), r.u16(), r.u32()
            score = {(3, 10): 5, (3, 1): 4, (0, 4): 4, (0, 3): 3,
                     (0, 6): 3, (3, 0): 2, (1, 0): 1}.get((pid, eid), 0)
            if score > best_score:
                best_score, best = score, base + off
        if best is None:
            return
        s = Reader(self.data, best)
        fmt = s.u16()
        if fmt == 4:
            s.u16(); s.u16()
            seg2 = s.u16()
            seg = seg2 // 2
            s.p += 6
            end = [s.u16() for _ in range(seg)]
            s.u16()
            start = [s.u16() for _ in range(seg)]
            delta = [s.s16() for _ in range(seg)]
            ro_pos = s.p
            rng = [s.u16() for _ in range(seg)]
            for i in range(seg):
                for c in range(start[i], min(end[i], 0xFFFF) + 1):
                    if rng[i] == 0:
                        g = (c + delta[i]) & 0xFFFF
                    else:
                        gp = ro_pos + i * 2 + rng[i] + (c - start[i]) * 2
                        if gp + 1 >= len(self.data):
                            continue
                        g = struct.unpack_from(">H", self.data, gp)[0]
                        if g:
                            g = (g + delta[i]) & 0xFFFF
                    if g:
                        self.cmap[c] = g
        elif fmt == 12:
            s.u16(); s.u32(); s.u32()
            ngroups = s.u32()
            for _ in range(ngroups):
                a, b, g = s.u32(), s.u32(), s.u32()
                if b - a > 0x10000:
                    b = a + 0x10000
                for c in range(a, b + 1):
                    self.cmap[c] = g + (c - a)
        elif fmt == 6:
            s.u16(); s.u16()
            first, cnt = s.u16(), s.u16()
            for i in range(cnt):
                self.cmap[first + i] = s.u16()
        elif fmt == 0:
            s.u16(); s.u16()
            for c in range(256):
                self.cmap[c] = s.u8()

    def gid(self, ch):
        return self.cmap.get(ord(ch), 0)

    def contours(self, gid, depth=0):
        """Glyph outline in font units: list of closed contours of (x, y)."""
        if self.cff is not None:
            return self.cff.glyph(gid)
        return self._glyf(gid, depth)

    # ------------------------------------------------------- TrueType outlines
    def _glyf(self, gid, depth=0):
        if self.loca is None or gid + 1 >= len(self.loca) or depth > 5:
            return []
        goff, glen = self.tables["glyf"][0] + self.loca[gid], \
            self.loca[gid + 1] - self.loca[gid]
        if glen == 0:
            return []
        r = Reader(self.data, goff)
        ncont = r.s16()
        r.p += 8
        if ncont < 0:
            return self._composite(r, depth)
        ends = [r.u16() for _ in range(ncont)]
        npts = (ends[-1] + 1) if ends else 0
        instr_len = r.u16()
        r.p += instr_len  # skip hinting instructions
        flags = []
        while len(flags) < npts:
            f = r.u8()
            flags.append(f)
            if f & 8:
                for _ in range(r.u8()):
                    flags.append(f)
        xs, v = [], 0
        for f in flags:
            if f & 2:
                d = r.u8()
                v += d if f & 16 else -d
            elif not f & 16:
                v += r.s16()
            xs.append(v)
        ys, v = [], 0
        for f in flags:
            if f & 4:
                d = r.u8()
                v += d if f & 32 else -d
            elif not f & 32:
                v += r.s16()
            ys.append(v)

        out, start = [], 0
        for e in ends:
            pts = [(xs[i], ys[i], bool(flags[i] & 1))
                   for i in range(start, min(e + 1, npts))]
            start = e + 1
            if pts:
                out.append(quad_contour(pts))
        return out

    def _composite(self, r, depth):
        out = []
        while True:
            flags, sub_gid = r.u16(), r.u16()
            if flags & 1:
                a1, a2 = r.s16(), r.s16()
            else:
                a1 = struct.unpack_from(">b", r.d, r.p)[0]
                a2 = struct.unpack_from(">b", r.d, r.p + 1)[0]
                r.p += 2
            xx = yy = 1.0
            xy = yx = 0.0
            if flags & 8:
                xx = yy = r.f2dot14()
            elif flags & 0x40:
                xx, yy = r.f2dot14(), r.f2dot14()
            elif flags & 0x80:
                xx, yx, xy, yy = (r.f2dot14(), r.f2dot14(),
                                  r.f2dot14(), r.f2dot14())
            dx, dy = (a1, a2) if flags & 2 else (0, 0)
            for c in self.contours(sub_gid, depth + 1):
                out.append([(x * xx + y * xy + dx, x * yx + y * yy + dy)
                            for x, y in c])
            if not flags & 0x20:
                break
        return out


def quad_contour(pts, steps=12):
    """Flatten one TrueType contour (on/off points) into a polyline."""
    n = len(pts)
    # Rotate so we begin on an on-curve point, synthesising one if needed.
    idx = next((i for i, p in enumerate(pts) if p[2]), None)
    if idx is None:
        x0 = (pts[0][0] + pts[-1][0]) / 2.0
        y0 = (pts[0][1] + pts[-1][1]) / 2.0
        seq = [(x0, y0, True)] + pts
    else:
        seq = pts[idx:] + pts[:idx]
    seq.append(seq[0])

    out = [(seq[0][0], seq[0][1])]
    i = 1
    cur = (seq[0][0], seq[0][1])
    while i < len(seq):
        x, y, on = seq[i]
        if on:
            out.append((x, y))
            cur = (x, y)
            i += 1
            continue
        nx, ny, non = seq[i + 1] if i + 1 < len(seq) else seq[0]
        if not non:  # implied on-curve midpoint between two controls
            nx, ny = (x + nx) / 2.0, (y + ny) / 2.0
            step = 1
        else:
            step = 2
        for s in range(1, steps + 1):
            t = s / steps
            u = 1 - t
            out.append((u * u * cur[0] + 2 * u * t * x + t * t * nx,
                        u * u * cur[1] + 2 * u * t * y + t * t * ny))
        cur = (nx, ny)
        i += step
    return out


# ------------------------------------------------------------------ CFF / Type2

STD_STRINGS_COUNT = 391


class CFF:
    """Just enough CFF to walk Type 2 charstrings into outlines."""

    def __init__(self, data, base):
        self.data = data
        r = Reader(data, base)
        r.u8(); r.u8()
        hdr_size = r.u8()
        r.u8()
        p = base + hdr_size
        p = self._skip_index(p)                    # Name INDEX
        top_dicts, p = self._index(p)              # Top DICT INDEX
        p = self._skip_index(p)                    # String INDEX
        self.gsubrs, p = self._index(p)            # Global Subr INDEX

        top = parse_dict(top_dicts[0])
        self.charstrings, _ = self._index(base + int(top[(0, 17)][0]))
        self.subrs = []
        self.nominal_width = 0
        if (0, 18) in top:
            psize, poff = top[(0, 18)]
            priv = parse_dict(data[base + int(poff):base + int(poff) + int(psize)])
            if (0, 19) in priv:
                self.subrs, _ = self._index(base + int(poff) + int(priv[(0, 19)][0]))
        self.font_matrix = top.get((12, 7), [0.001, 0, 0, 0.001, 0, 0])

        # CID-keyed fonts keep per-FD private subrs; resolve them via FDSelect.
        self.fd_subrs = None
        self.fdselect = None
        if (12, 36) in top:
            fdarray, _ = self._index(base + int(top[(12, 36)][0]))
            self.fd_subrs = []
            for fd in fdarray:
                d = parse_dict(fd)
                s = []
                if (0, 18) in d:
                    ps, po = d[(0, 18)]
                    pv = parse_dict(data[base + int(po):base + int(po) + int(ps)])
                    if (0, 19) in pv:
                        s, _ = self._index(base + int(po) + int(pv[(0, 19)][0]))
                self.fd_subrs.append(s)
            if (12, 37) in top:
                self.fdselect = self._read_fdselect(base + int(top[(12, 37)][0]))

    def _read_fdselect(self, off):
        r = Reader(self.data, off)
        fmt = r.u8()
        sel = {}
        if fmt == 0:
            for g in range(len(self.charstrings)):
                sel[g] = r.u8()
        elif fmt == 3:
            nr = r.u16()
            first = r.u16()
            for _ in range(nr):
                fd = r.u8()
                nxt = r.u16()
                for g in range(first, nxt):
                    sel[g] = fd
                first = nxt
        return sel

    def _index(self, pos):
        r = Reader(self.data, pos)
        count = r.u16()
        if count == 0:
            return [], pos + 2
        osize = r.u8()
        offs = []
        for _ in range(count + 1):
            v = 0
            for _ in range(osize):
                v = (v << 8) | r.u8()
            offs.append(v)
        base = r.p - 1
        items = [self.data[base + offs[i]:base + offs[i + 1]] for i in range(count)]
        return items, base + offs[-1]

    def _skip_index(self, pos):
        return self._index(pos)[1]

    def glyph(self, gid):
        if gid >= len(self.charstrings):
            return []
        subrs = self.subrs
        if self.fd_subrs is not None:
            fd = (self.fdselect or {}).get(gid, 0)
            if fd < len(self.fd_subrs):
                subrs = self.fd_subrs[fd]
        t = Type2(self.charstrings, self.gsubrs, subrs)
        t.run(self.charstrings[gid])
        t.close()
        m = self.font_matrix
        if abs(m[0] - 0.001) > 1e-9 or m[1] or m[2] or abs(m[3] - 0.001) > 1e-9:
            k = 1000.0
            return [[((x * m[0] + y * m[2] + m[4]) * k,
                      (x * m[1] + y * m[3] + m[5]) * k) for x, y in c]
                    for c in t.contours]
        return t.contours


def parse_dict(b):
    out, ops, i = {}, [], 0
    while i < len(b):
        v = b[i]
        if v <= 21:
            if v == 12:
                key = (12, b[i + 1])
                i += 2
            else:
                key = (0, v)
                i += 1
            out[key] = ops
            ops = []
        elif v == 28:
            ops.append(struct.unpack_from(">h", b, i + 1)[0])
            i += 3
        elif v == 29:
            ops.append(struct.unpack_from(">i", b, i + 1)[0])
            i += 5
        elif v == 30:  # real number, nibble-encoded
            s, i = "", i + 1
            done = False
            while i < len(b) and not done:
                for nib in (b[i] >> 4, b[i] & 15):
                    if nib <= 9:
                        s += chr(48 + nib)
                    elif nib == 10:
                        s += "."
                    elif nib == 11:
                        s += "E"
                    elif nib == 12:
                        s += "E-"
                    elif nib == 14:
                        s += "-"
                    elif nib == 15:
                        done = True
                        break
                i += 1
            ops.append(float(s or 0))
        elif 32 <= v <= 246:
            ops.append(v - 139)
            i += 1
        elif 247 <= v <= 250:
            ops.append((v - 247) * 256 + b[i + 1] + 108)
            i += 2
        elif 251 <= v <= 254:
            ops.append(-(v - 251) * 256 - b[i + 1] - 108)
            i += 2
        else:
            i += 1
    return out


def bias(subrs):
    n = len(subrs)
    return 107 if n < 1240 else (1131 if n < 33900 else 32768)


class Type2:
    """Type 2 charstring interpreter producing flattened contours."""

    CURVE_STEPS = 14

    def __init__(self, charstrings, gsubrs, subrs):
        self.gsubrs, self.subrs = gsubrs, subrs
        self.gbias, self.lbias = bias(gsubrs), bias(subrs)
        self.stack = []
        self.contours = []
        self.cur = []
        self.x = self.y = 0.0
        self.nstems = 0
        self.width_done = False
        self.trans = []

    def close(self):
        if len(self.cur) > 1:
            self.contours.append(self.cur)
        self.cur = []

    def moveto(self, x, y):
        self.close()
        self.x, self.y = x, y
        self.cur = [(x, y)]

    def lineto(self, x, y):
        self.x, self.y = x, y
        self.cur.append((x, y))

    def curveto(self, x1, y1, x2, y2, x3, y3):
        x0, y0 = self.x, self.y
        for s in range(1, self.CURVE_STEPS + 1):
            t = s / self.CURVE_STEPS
            u = 1 - t
            a, b, c, d = u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t
            self.cur.append((a * x0 + b * x1 + c * x2 + d * x3,
                             a * y0 + b * y1 + c * y2 + d * y3))
        self.x, self.y = x3, y3

    def _take_width(self, even):
        if not self.width_done:
            self.width_done = True
            if len(self.stack) % 2 == (1 if even else 0) and self.stack:
                self.stack.pop(0)

    def run(self, code, depth=0):
        if depth > 10:
            return True
        i = 0
        st = self.stack
        while i < len(code):
            b0 = code[i]
            if b0 >= 32 or b0 == 28:
                if b0 == 28:
                    st.append(struct.unpack_from(">h", code, i + 1)[0])
                    i += 3
                elif b0 <= 246:
                    st.append(b0 - 139)
                    i += 1
                elif b0 <= 250:
                    st.append((b0 - 247) * 256 + code[i + 1] + 108)
                    i += 2
                elif b0 <= 254:
                    st.append(-(b0 - 251) * 256 - code[i + 1] - 108)
                    i += 2
                else:
                    st.append(struct.unpack_from(">i", code, i + 1)[0] / 65536.0)
                    i += 5
                continue
            i += 1
            if b0 in (1, 3, 18, 23):        # h/v stem(hm)
                self._take_width(True)
                self.nstems += len(st) // 2
                del st[:]
            elif b0 in (19, 20):            # hintmask / cntrmask
                self._take_width(True)
                self.nstems += len(st) // 2
                del st[:]
                i += (self.nstems + 7) // 8
            elif b0 == 21:                  # rmoveto
                self._take_width(True)
                self.moveto(self.x + st[-2], self.y + st[-1])
                del st[:]
            elif b0 == 22:                  # hmoveto
                self._take_width(False)
                self.moveto(self.x + st[-1], self.y)
                del st[:]
            elif b0 == 4:                   # vmoveto
                self._take_width(False)
                self.moveto(self.x, self.y + st[-1])
                del st[:]
            elif b0 == 5:                   # rlineto
                for j in range(0, len(st) - 1, 2):
                    self.lineto(self.x + st[j], self.y + st[j + 1])
                del st[:]
            elif b0 in (6, 7):              # hlineto / vlineto
                horiz = (b0 == 6)
                for v in st:
                    if horiz:
                        self.lineto(self.x + v, self.y)
                    else:
                        self.lineto(self.x, self.y + v)
                    horiz = not horiz
                del st[:]
            elif b0 == 8:                   # rrcurveto
                for j in range(0, len(st) - 5, 6):
                    self._rc(st[j:j + 6])
                del st[:]
            elif b0 == 24:                  # rcurveline
                j = 0
                while len(st) - j >= 8:
                    self._rc(st[j:j + 6])
                    j += 6
                self.lineto(self.x + st[j], self.y + st[j + 1])
                del st[:]
            elif b0 == 25:                  # rlinecurve
                j = 0
                while len(st) - j >= 8:
                    self.lineto(self.x + st[j], self.y + st[j + 1])
                    j += 2
                self._rc(st[j:j + 6])
                del st[:]
            elif b0 in (26, 27):            # vvcurveto / hhcurveto
                j = 0
                d1 = 0.0
                if len(st) % 4:
                    d1 = st[0]
                    j = 1
                while j + 3 < len(st):
                    a, b, c, d = st[j:j + 4]
                    if b0 == 26:
                        self._rc([d1, a, b, c, 0, d])
                    else:
                        self._rc([a, d1, b, c, d, 0])
                    d1 = 0.0
                    j += 4
                del st[:]
            elif b0 in (30, 31):            # vhcurveto / hvcurveto
                horiz = (b0 == 31)
                j = 0
                while j + 3 < len(st):
                    last = (len(st) - j == 5)
                    a, b, c, d = st[j:j + 4]
                    e = st[j + 4] if last else 0.0
                    if horiz:
                        self._rc([a, 0, b, c, e, d])
                    else:
                        self._rc([0, a, b, c, d, e])
                    horiz = not horiz
                    j += 4
                del st[:]
            elif b0 == 10:                  # callsubr
                if st:
                    idx = int(st.pop()) + self.lbias
                    if 0 <= idx < len(self.subrs):
                        if self.run(self.subrs[idx], depth + 1):
                            return True
            elif b0 == 29:                  # callgsubr
                if st:
                    idx = int(st.pop()) + self.gbias
                    if 0 <= idx < len(self.gsubrs):
                        if self.run(self.gsubrs[idx], depth + 1):
                            return True
            elif b0 == 11:                  # return
                return False
            elif b0 == 14:                  # endchar
                self._take_width(True)
                del st[:]
                return True
            elif b0 == 12:
                b1 = code[i]
                i += 1
                if b1 == 35:                # flex
                    self._rc(st[0:6])
                    self._rc(st[6:12])
                elif b1 == 34:              # hflex
                    y0 = self.y
                    self._rc([st[0], 0, st[1], st[2], st[3], 0])
                    self._rc([st[4], 0, st[5], y0 - (self.y), st[6], 0])
                elif b1 == 36:              # hflex1
                    y0 = self.y
                    self._rc([st[0], st[1], st[2], st[3], st[4], 0])
                    self._rc([st[5], 0, st[6], st[7], st[8], y0 - self.y])
                elif b1 == 37:              # flex1
                    x0, y0 = self.x, self.y
                    dx = sum(st[0:10:2])
                    dy = sum(st[1:11:2])
                    self._rc(st[0:6])
                    self._rc([st[6], st[7], st[8], st[9],
                              x0 + dx + st[10] - self.x - st[6] - st[8],
                              y0 + dy - self.y - st[7] - st[9]])
                del st[:]
            else:
                del st[:]
        return False

    def _rc(self, a):
        x1, y1 = self.x + a[0], self.y + a[1]
        x2, y2 = x1 + a[2], y1 + a[3]
        self.curveto(x1, y1, x2, y2, x2 + a[4], y2 + a[5])


class BitmapFont(Font):
    """An OTB (bitmap-only OpenType): glyph bitmaps straight from EBDT/EBLC.

    No outlines exist in such a file, so the vector pipeline has nothing to
    chew on. Instead each glyph serves its scanline bitmap (and its
    smallGlyphMetrics) at the file's native pixel size; bigger logo sizes
    come from scaling the finished mask, never from FreeType.

    Index subtable formats 1 (explicit offsets) and 2 (constant-size slots)
    are covered, which is every layout Terminus .otb files use.
    """

    def __init__(self, path):
        Font.__init__(self, path)
        if "EBLC" not in self.tables or "EBDT" not in self.tables:
            raise SystemExit(1)
        self.strikes = []
        ebdt = self.tables["EBDT"][0]
        eblc = self.tables["EBLC"][0]
        r = Reader(self.data, eblc)
        r.u32()  # version
        for _ in range(r.u32()):
            st = r.p
            idx_off, idx_size, num_idx = r.u32(), r.u32(), r.u32()
            r.u32()                          # colorRef
            asc = self.data[st + 16]
            desc = self.data[st + 17] - 256 if self.data[st + 17] >= 128 \
                else self.data[st + 17]
            start_g, end_g = Reader(self.data, st + 40).u16(), 0
            end_g = Reader(self.data, st + 42).u16()
            r.seek(st + 48)
            ranges = []
            for k in range(num_idx):
                s = Reader(self.data, eblc + idx_off + 8 * k)
                first, last = s.u16(), s.u16()
                sub = eblc + idx_off + s.u32()
                s = Reader(self.data, sub)
                index_fmt, image_fmt = s.u16(), s.u16()
                image_off = s.u32()
                if index_fmt not in (1, 2):
                    continue
                slots = []
                if index_fmt == 2:
                    image_size = s.u32()
                    for g in range(first, last + 1):
                        slots.append(ebdt + image_off + image_size * (g - first))
                else:
                    for g in range(first, last + 1):
                        slots.append(ebdt + image_off + s.u32())
                ranges.append({"first": first, "last": last,
                               "image_fmt": image_fmt, "slots": slots})
            self.strikes.append({"cell": asc - desc, "asc": asc,
                                 "desc": desc, "ranges": ranges})

    def pick_strike(self, box):
        """The strike whose cell height sits closest to the render box.

        `box` is the same inner box the vector path fills; ties keep the
        first strike seen. Returns None if the file serves no strikes.
        """
        best = None
        for st in self.strikes:
            for rng in st["ranges"]:
                cand = (abs(st["cell"] - box), st, rng)
                if best is None or cand[0] < best[0]:
                    best = cand
        return best

    def bitmap(self, st, rng, g):
        """(rows, w, h, bx, by, adv) for a gid, or None if out of range."""
        if not (rng["first"] <= g <= rng["last"]):
            return None
        off = rng["slots"][g - rng["first"]]
        r = Reader(self.data, off)
        h, w, bx, by, adv = r.u8(), r.u8(), r.s8(), r.s8(), r.u8()
        if rng["image_fmt"] != 1 or w <= 0 or h <= 0:
            return (b"", w, h, bx, by, adv)
        rowsz = (w + 7) // 8
        rows = self.data[off + 5:off + 5 + rowsz * h]
        return (rows, w, h, bx, by, adv)


# ------------------------------------------------------------------ rasterizer

def rasterize(contours, width, height, ss=4):
    """Nonzero-winding scanline fill with ss x ss supersampling.

    Returns a bytearray of width*height coverage values, 0..255.
    """
    edges = []
    for c in contours:
        n = len(c)
        for i in range(n):
            x0, y0 = c[i]
            x1, y1 = c[(i + 1) % n]
            if y0 != y1:
                edges.append((y0, y1, x0, (x1 - x0) / (y1 - y0)))
    cov = bytearray(width * height)
    if not edges:
        return cov
    ymin = max(0, int(min(min(e[0], e[1]) for e in edges) * ss))
    ymax = min(height * ss - 1, int(max(max(e[0], e[1]) for e in edges) * ss) + 1)

    acc = [0] * (width + 2)
    inv = 255.0 / (ss * ss)
    for sy in range(ymin, ymax + 1):
        y = (sy + 0.5) / ss
        xs = []
        for y0, y1, x0, slope in edges:
            if (y0 <= y < y1) or (y1 <= y < y0):
                xs.append((x0 + (y - y0) * slope, 1 if y1 > y0 else -1))
        if not xs:
            continue
        xs.sort()
        wind = 0
        spans = []
        for j, (xv, d) in enumerate(xs):
            prev = wind
            wind += d
            if prev == 0 and wind != 0:
                start = xv
            elif prev != 0 and wind == 0:
                spans.append((start, xv))
        if not spans:
            continue
        row = (sy // ss) * width
        for xa, xb in spans:
            # sub-sample horizontally on the same grid
            ia = int(xa * ss + 0.5)
            ib = int(xb * ss + 0.5)
            if ib <= ia:
                continue
            ia = max(ia, 0)
            ib = min(ib, width * ss)
            pa, pb = ia // ss, (ib - 1) // ss
            if pa == pb:
                if 0 <= pa < width:
                    acc[pa] += ib - ia
            else:
                if 0 <= pa < width:
                    acc[pa] += (pa + 1) * ss - ia
                for px in range(pa + 1, pb):
                    if 0 <= px < width:
                        acc[px] += ss
                if 0 <= pb < width:
                    acc[pb] += ib - pb * ss
        for px in range(width):
            if acc[px]:
                v = cov[row + px] + int(acc[px] * inv + 0.5)
                cov[row + px] = 255 if v > 255 else v
                acc[px] = 0
    return cov


# ------------------------------------------------------------------- PNG write

def chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def write_png(path, width, height, rgba):
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)  # filter type 0 (None) - keeps this readable and lossless
        raw += rgba[y * stride:(y + 1) * stride]
    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    out += chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(out)

# ---------------------------------------------------------------------- colors

# Plain-English color names. Anything here, or any #hex, works everywhere.
NAMED = {
    "black": "#000000", "white": "#ffffff", "ink": "#14161a",
    "cream": "#f4efe4", "paper": "#f4efe4", "red": "#c0392b",
    "crimson": "#a01f2d", "blue": "#1f4e8c", "sky": "#3d8bd4",
    "green": "#16733f", "mint": "#57c99a", "gold": "#c8a34a",
    "amber": "#e0913a", "slate": "#2b3440", "charcoal": "#22252b",
    "purple": "#5b3a8c", "pink": "#d95f8a", "teal": "#177f83",
    "sand": "#e2d3b3", "clear": None,
}

# Ready-made color pairs, offered by name in the wizard and via --colors.
THEMES = [
    ("midnight", "Cream on near-black", "cream", "ink"),
    ("classic",  "Black on cream paper", "#101010", "cream"),
    ("crimson",  "White on deep red", "white", "crimson"),
    ("gold",     "Gold on slate blue", "gold", "slate"),
    ("mint",     "Mint on charcoal", "mint", "charcoal"),
    ("ocean",    "Cream on ocean blue", "cream", "blue"),
    ("plum",     "Sand on purple", "sand", "purple"),
    ("mono",     "White on black", "white", "black"),
    ("cutout",   "Black glyph, no background", "ink", "clear"),
]
THEME_MAP = {k: (f, b) for k, _, f, b in THEMES}

# Every palette again, colors swapped: midnight-r is ink text on cream.
for _k, _l, _f, _b in list(THEMES):
    THEMES.append((_k + "-r", _l + " - reversed", _b, _f))
    THEME_MAP[_k + "-r"] = (_b, _f)

DEFAULT_THEME = "midnight"


def parse_color(s):
    """Accept a name, a #hex (3/6/8 digits), or 'none' for transparent."""
    if s is None:
        return None
    s = str(s).strip()
    low = s.lower()
    if low in ("none", "transparent", "clear", ""):
        return None
    if low in NAMED:
        s = NAMED[low]
        if s is None:
            return None
    s = s.lstrip("#")
    if len(s) == 3:
        s = "".join(c * 2 for c in s)
    if len(s) == 6:
        s += "ff"
    if len(s) != 8 or any(c not in "0123456789abcdefABCDEF" for c in s):
        raise ValueError(s)
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4, 6))


def color_or_quit(value, what):
    try:
        return parse_color(value)
    except ValueError:
        say("Sorry, I don't recognise the %s color %r." % (what, value))
        say("Use a color name, a hex code like #c0392b, or 'none'.")
        say("Names available: " + ", ".join(sorted(NAMED)))
        raise SystemExit(1)


# ------------------------------------------------------------- persistence

def bundles_path():
    """Where the saved bundles (style settings, no text) live."""
    return os.path.join(HERE, "bundles.json")


def load_bundles():
    """Read saved bundles from disk. Returns [] if none exist yet."""
    path = bundles_path()
    if not os.path.exists(path):
        return []
    try:
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        if isinstance(data, list):
            return data
        return []
    except (json.JSONDecodeError, OSError):
        return []


def save_bundles(bundles):
    """Write the bundle list to disk."""
    path = bundles_path()
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(bundles, fh, indent=2, ensure_ascii=False)
        fh.write("\n")


# ------------------------------------------------------------------------ main
# ------------------------------------------------------------------------ main

def layout(font, text, line_spacing=1.25):
    """Lay the text out in font units, honouring spaces, tabs and newlines.

    Every line is drawn separately and centred over the others, so a block of
    several lines stacks the way you typed it.
    """
    text = text.replace("\t", "    ")
    missing = []
    upem = font.units_per_em or 1000
    line_height = upem * line_spacing
    space_adv = font.advance(font.gid(" ")) if font.gid(" ") else upem // 3

    rows = []
    for line in text.split("\n"):
        shapes, pen = [], 0.0
        for ch in line:
            if ch == " ":
                pen += space_adv
                continue
            g = font.gid(ch)
            if g == 0 and ch not in font.cmap:
                if ch not in missing:
                    missing.append(ch)
                continue
            for c in font.contours(g):
                shapes.append([(x + pen, y) for x, y in c])
            pen += font.advance(g)
        rows.append((shapes, pen))

    out = []
    for i, (shapes, width) in enumerate(rows):
        dx, dy = -width / 2.0, -i * line_height
        for c in shapes:
            out.append([(x + dx, y + dy) for x, y in c])

    if missing:
        say("Note: this style has no drawing for %s, so it was left out."
            % " ".join(repr(c) for c in missing))
    return out


def render(text, font_path, size, fg, bg, margin):
    if is_bitmap_font(font_path):
        return render_bitmap(text, font_path, size, fg, bg, margin)
    font = Font(font_path)
    shapes = layout(font, text)
    if not shapes:
        raise SystemExit(1)

    xs = [p[0] for c in shapes for p in c]
    ys = [p[1] for c in shapes for p in c]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    w, h = max(x1 - x0, 1e-6), max(y1 - y0, 1e-6)

    box = size * (1.0 - 2 * margin)
    scale = box / max(w, h)
    ox = (size - w * scale) / 2.0 - x0 * scale
    oy = (size - h * scale) / 2.0 - y0 * scale

    # y flips: font units go up, image rows go down
    dev = [[(x * scale + ox, size - (y * scale + oy)) for x, y in c]
           for c in shapes]

    cov = rasterize(dev, size, size)

    br, bgc, bb, ba = bg if bg else (fg[0], fg[1], fg[2], 0)
    fr, fgc, fb, fa = fg
    px = bytearray(size * size * 4)
    for i in range(size * size):
        a = cov[i]
        j = i * 4
        if a == 0:
            px[j:j + 4] = bytes((br, bgc, bb, ba))
        elif a == 255 and fa == 255:
            px[j:j + 4] = bytes((fr, fgc, fb, 255))
        else:
            t = a / 255.0 * (fa / 255.0)
            u = 1 - t
            oa = ba / 255.0 * u + t
            if oa <= 0:
                px[j:j + 4] = b"\0\0\0\0"
            else:
                px[j] = int((br * (ba / 255.0) * u + fr * t) / oa)
                px[j + 1] = int((bgc * (ba / 255.0) * u + fgc * t) / oa)
                px[j + 2] = int((bb * (ba / 255.0) * u + fb * t) / oa)
                px[j + 3] = int(oa * 255 + 0.5)
    return px


def render_bitmap(text, font_path, size, fg, bg, margin):
    """render() for bitmap-only OTB faces: EBDT strikes instead of outlines.

    The glyphs are placed at the file's native pixel size, blitted into one
    1-bit mask, and that mask is nearest-neighbour scaled into the very same
    inner box the vector path fills, so both styles obey --size and
    --padding identically. The color compose loop below is the vector one,
    verbatim, with coverage coming from the mask.
    """
    font = BitmapFont(font_path)
    if not font.strikes:
        raise SystemExit(1)

    text = text.replace("\t", "    ")
    missing = []
    picked = font.pick_strike(size * (1.0 - 2 * margin))
    if picked is None:
        raise SystemExit(1)
    _, strike, rng = picked
    cell = strike["cell"]
    line_height = cell * 1.25

    space = font.gid(" ")
    space_bm = font.bitmap(strike, rng, space) if space else None
    space_adv = (space_bm[5] if space_bm else 0) or cell // 3

    rects = []   # (x, by, g, w, h): x = pen + bearingX, by = bearingY up
    pen_rows = []
    for line in text.split("\n"):
        pen = 0.0
        for ch in line:
            if ch == " ":
                pen += space_adv
                continue
            g = font.gid(ch)
            if g == 0 and ch not in font.cmap:
                if ch not in missing:
                    missing.append(ch)
                continue
            bm = font.bitmap(strike, rng, g)
            if bm is None:
                if ch not in missing:
                    missing.append(ch)
                continue
            _, w, h, bx, by, adv = bm
            if w > 0 and h > 0:
                rects.append((pen + bx, by, g, w, h))
            pen += adv
        pen_rows.append((rects, pen))
        rects = []

    # resolve per-row vertical placement now that row indices exist
    placed = []
    for i, (row_rects, width) in enumerate(pen_rows):
        dx = -width / 2.0
        baseline = i * line_height
        for (x, by, g, w, h) in row_rects:
            placed.append((x + dx, baseline - by, g, w, h))

    if missing:
        say("Note: this style has no drawing for %s, so it was left out."
            % " ".join(repr(c) for c in missing))
    if not placed:
        raise SystemExit(1)

    # one 1-bit mask at strike resolution, bounds from the rounded rects
    cells = []
    for (x, y, g, w, h) in placed:
        gx, gy = int(math.floor(x + 0.5)), int(math.floor(y + 0.5))
        cells.append((gx, gy, g, w, h))
    min_x = min(c[0] for c in cells)
    min_y = min(c[1] for c in cells)
    bw = max(c[0] + c[3] for c in cells) - min_x
    bh = max(c[1] + c[4] for c in cells) - min_y
    mask = bytearray(bw * bh)
    for (gx, gy, g, w, h) in cells:
        rows, _, _, _, _, _ = font.bitmap(strike, rng, g)
        rowsz = (w + 7) // 8
        for r in range(h):
            orow = rows[r * rowsz:(r + 1) * rowsz]
            base = (gy - min_y + r) * bw + (gx - min_x)
            for c in range(w):
                if orow[c // 8] >> (7 - (c % 8)) & 1:
                    mask[base + c] = 255

    # nearest-neighbour into the same inner box the vector path fills
    w, h = bw, bh
    box = size * (1.0 - 2 * margin)
    scale = box / max(w, h)
    tw = max(int(w * scale + 0.5), 1)
    th = max(int(h * scale + 0.5), 1)
    ox = (size - tw) // 2
    oy = (size - th) // 2
    cov = bytearray(size * size)
    for ty in range(th):
        sy = ty * bh // th
        srow = sy * bw
        orow = (oy + ty) * size + ox
        for tx in range(tw):
            if mask[srow + tx * bw // tw]:
                cov[orow + tx] = 255

    br, bgc, bb, ba = bg if bg else (fg[0], fg[1], fg[2], 0)
    fr, fgc, fb, fa = fg
    px = bytearray(size * size * 4)
    for i in range(size * size):
        a = cov[i]
        j = i * 4
        if a == 0:
            px[j:j + 4] = bytes((br, bgc, bb, ba))
        elif a == 255 and fa == 255:
            px[j:j + 4] = bytes((fr, fgc, fb, 255))
        else:
            t = a / 255.0 * (fa / 255.0)
            u = 1 - t
            oa = ba / 255.0 * u + t
            if oa <= 0:
                px[j:j + 4] = b"\0\0\0\0"
            else:
                px[j] = int((br * (ba / 255.0) * u + fr * t) / oa)
                px[j + 1] = int((bgc * (ba / 255.0) * u + fgc * t) / oa)
                px[j + 2] = int((bb * (ba / 255.0) * u + fb * t) / oa)
                px[j + 3] = int(oa * 255 + 0.5)
    return px


def is_bitmap_font(font_path):
    """True when the sfnt table directory lists an EBDT table."""
    with open(font_path, "rb") as fh:
        head = fh.read(12 + 16 * 512)
    num = struct.unpack_from(">H", head, 4)[0]
    for i in range(num):
        off = 12 + 16 * i
        if off + 16 > len(head):
            break
        if head[off:off + 4] == b"EBDT":
            return True
    return False


# ------------------------------------------------------- lists of logos

EXAMPLE_LIST = """\
# A list of logos. Run it with:   ./logo --from logos.txt
#
# Anything after a # is a note and is ignored.
# Each logo starts with a name in [brackets]. That name becomes the file
# name, so the picture below lands in pictures/whatsapp.png
#
# Settings you can use in any block:
#   style       one of the names from  ./logo --styles
#   colors      one of the pairs from  ./logo --colors-list
#   text-color  a color name or #hex, overrides the pair
#   background  a color name, #hex, or none for transparent
#   size        width and height in pixels
#   padding     empty space around the letters, 0 to 40 percent
#   text        what to draw

# Anything in [defaults] applies to every logo below, unless that logo
# says otherwise. 'folder' chooses where the pictures are written.
[defaults]
folder = pictures
size   = 640

[whatsapp]
style  = medieval
colors = midnight
text   = N

[work-avatar]
style  = modern
colors = crimson
text   = JD

# For text with spaces, tabs or several lines, leave 'text:' empty and
# indent the lines below it. They are drawn exactly as you typed them.
[poster]
style   = poster
colors  = gold
size    = 1024
padding = 8
text:
    GOOD
    NIGHT

[sticker]
style      = brush
text-color = ink
background = none
text       = OK
"""

# Settings a block may set, and the friendly name each maps to internally.
LIST_KEYS = {
    "style": "style", "colors": "colors", "color": "colors",
    "text-color": "fg", "textcolor": "fg", "text color": "fg",
    "background": "bg", "background-color": "bg",
    "size": "size", "padding": "padding", "text": "text",
    "folder": "folder", "name": "name",
}


# What we show when someone mistypes a setting name.
SETTING_HELP = ("style", "colors", "text-color", "background", "size",
                "padding", "text", "folder")


class ListError(Exception):
    """A problem in the user's list file, described in plain words."""


def read_list(path):
    """Parse a list file into (folder, [logo, logo, ...])."""
    try:
        with open(path, "r", encoding="utf-8") as fh:
            raw = fh.read().split("\n")
    except OSError as e:
        raise ListError("I couldn't open %s (%s)." % (path, e.strerror))

    defaults, blocks = {}, []
    current = None
    i = 0
    while i < len(raw):
        line = raw[i]
        i += 1
        bare = line.strip()
        if not bare or bare.startswith("#"):
            continue

        if bare.startswith("[") and bare.endswith("]"):
            name = bare[1:-1].strip()
            if not name:
                raise ListError("Line %d: a block needs a name, like [logo1]."
                                % i)
            if name.lower() == "defaults":
                current = defaults
            else:
                current = {"name": name}
                blocks.append(current)
            continue

        if current is None:
            raise ListError(
                "Line %d: this setting comes before any [name] block.\n"
                "  Start a logo with a name in brackets first, like [logo1]."
                % i)

        sep = min([p for p in (bare.find("="), bare.find(":")) if p > 0]
                  or [-1])
        if sep < 0:
            raise ListError("Line %d: I expected  setting = value  here,\n"
                            "  but found: %s" % (i, bare))
        key = bare[:sep].strip().lower()
        value = bare[sep + 1:].strip()
        if key not in LIST_KEYS:
            raise ListError(
                "Line %d: I don't know the setting %r.\n"
                "  You can use: %s" % (i, key, ", ".join(SETTING_HELP)))

        if LIST_KEYS[key] == "text" and not value:
            # An empty 'text:' means the indented lines below are the text.
            body = []
            while i < len(raw):
                nxt = raw[i]
                if nxt.strip() and not nxt[:1].isspace():
                    break
                body.append(nxt)
                i += 1
            while body and not body[0].strip():
                body.pop(0)
            while body and not body[-1].strip():
                body.pop()
            if not body:
                raise ListError("Line %d: 'text:' is empty, and no indented "
                                "lines follow it." % i)
            indents = [len(b) - len(b.lstrip(" \t"))
                       for b in body if b.strip()]
            cut = min(indents) if indents else 0
            value = "\n".join(b[cut:] if len(b) >= cut else b for b in body)
        elif LIST_KEYS[key] == "text":
            value = value.replace("\\n", "\n")

        current[LIST_KEYS[key]] = value

    folder = defaults.pop("folder", "pictures")
    logos = []
    for b in blocks:
        item = dict(defaults)
        item.pop("name", None)
        item.pop("folder", None)
        item.update(b)
        if not item.get("text"):
            raise ListError("The logo [%s] has no text to draw.\n"
                            "  Add a line such as:  text = %s"
                            % (item["name"], item["name"][:2].upper()))
        logos.append(item)
    if not logos:
        raise ListError("There are no logos in %s yet.\n"
                        "  A logo looks like:\n\n    [my-logo]\n"
                        "    style = modern\n    text  = AB" % path)
    return folder, logos


def look_settings(item, where):
    """Check style, colors, size and padding; return render options (no text).

    Shared by list files and bundles, so both describe the same "look".
    """
    style = item.get("style", DEFAULT_STYLE)
    if style not in STYLE_FILE:
        raise ListError("%s: there is no style called %r.\n"
                        "  Run  ./logo --styles  to see the choices."
                        % (where, style))
    theme = item.get("colors", DEFAULT_THEME)
    if theme not in THEME_MAP:
        raise ListError("%s: there is no color pair called %r.\n"
                        "  Run  ./logo --colors-list  to see the choices."
                        % (where, theme))
    fg_name, bg_name = THEME_MAP[theme]
    fg_name = item.get("fg") or fg_name
    bg_name = item.get("bg") or bg_name
    try:
        fg, bg = parse_color(fg_name), parse_color(bg_name)
    except ValueError as e:
        raise ListError("%s: I don't recognise the color %r.\n"
                        "  Use a color name, a hex code like #c0392b, "
                        "or 'none'." % (where, str(e)))
    if fg is None:
        raise ListError("%s: the letters need a visible color; 'none' only "
                        "works for the background." % where)

    def number(key, default, low, high):
        raw = item.get(key, default)
        try:
            v = int(str(raw))
        except ValueError:
            raise ListError("%s: %s should be a whole number, not %r."
                            % (where, key, raw))
        if not low <= v <= high:
            raise ListError("%s: %s should be between %d and %d."
                            % (where, key, low, high))
        return v

    size = number("size", 640, 16, 4096)
    padding = number("padding", 14, 0, 40)
    return dict(style=style, path=os.path.join(FONTDIR, STYLE_FILE[style]),
                fg=fg, bg=bg, size=size, margin=padding / 100.0)


def settings_from(item, where):
    """Turn one block's words into checked values, or explain what is wrong."""
    opts = look_settings(item, where)
    opts["text"] = item["text"]
    return opts


def bundle_to_settings(bundle, where):
    """Turn a saved bundle into render options (no text)."""
    item = {
        "style": bundle.get("style", DEFAULT_STYLE),
        "colors": bundle.get("colors", DEFAULT_THEME),
        "fg": bundle.get("text-color", bundle.get("fg")),
        "bg": bundle.get("background", bundle.get("bg")),
        "size": bundle.get("size", 640),
        "padding": bundle.get("padding", 14),
    }
    return look_settings(item, where)


def safe_name(name):
    keep = "".join(c if (c.isalnum() or c in "-_") else "-" for c in name)
    return keep.strip("-") or "logo"


def run_list(path, out_dir=None):
    folder, logos = read_list(path)
    folder = out_dir or folder
    if not os.path.isabs(folder):
        folder = os.path.join(HERE, folder)

    # Check every block first, so a mistake near the end never leaves you
    # with half a folder of pictures.
    plan = [(item, settings_from(item, "the logo [%s]" % item["name"]))
            for item in logos]

    seen = {}
    for item, _ in plan:
        f = safe_name(item["name"])
        if f in seen:
            raise ListError("Two logos would be saved as %s.png.\n"
                            "  Give [%s] a different name."
                            % (f, item["name"]))
        seen[f] = True

    os.makedirs(folder, exist_ok=True)
    say()
    say("Reading %s - %d logo%s to draw."
        % (path, len(logos), "" if len(logos) == 1 else "s"))
    say("Writing into %s" % folder)
    say()

    made = 0
    for item, opts in plan:
        out = os.path.join(folder, safe_name(item["name"]) + ".png")
        px = render(opts["text"], opts["path"], opts["size"], opts["fg"],
                    opts["bg"], opts["margin"])
        write_png(out, opts["size"], opts["size"], px)
        first = opts["text"].split("\n")[0]
        shown = first if "\n" not in opts["text"] else first + " ..."
        say("  %-22s %4d x %-4d  %-14s %s"
            % (os.path.basename(out), opts["size"], opts["size"],
               opts["style"], shown))
        made += 1

    say()
    say("Done - %d picture%s in %s"
        % (made, "" if made == 1 else "s", folder))


# ------------------------------------------------------------------- texts

def texts_file():
    """The one place the words live."""
    return os.path.join(HERE, "texts.txt")


LABEL_RE = re.compile(r"^\[(.+)\]$")


def entry_from_lines(lines):
    """Turn the lines of one block into a labeled text.

    A first line like [name] chooses the file name; without one, the
    name comes from the words themselves.
    """
    body = list(lines)
    label = None
    if body:
        m = LABEL_RE.match(body[0].strip())
        if m:
            label = safe_name(m.group(1).strip())
            body = body[1:]
    if not label:
        label = safe_name(body[0]) if body else "text"
    return {"label": label, "text": "\n".join(body)}


def unescape_line(line):
    r"""Turn \. into . and \\ into \, so a lone dot can still be typed."""
    return (line.replace("\\\\", "\x00").replace("\\.", ".")
            .replace("\x00", "\\").replace("\\t", "\t"))


def read_texts(path):
    """Read a texts file: one labeled text per block, blank line between."""
    try:
        with open(path, "r", encoding="utf-8") as fh:
            raw = fh.read().replace("\r\n", "\n")
    except OSError as e:
        raise ListError("I couldn't open %s (%s)." % (path, e.strerror))

    entries = []
    for block in raw.split("\n\n"):
        lines = [ln.rstrip() for ln in block.split("\n")
                 if ln.strip() and not ln.lstrip().startswith("#")]
        if lines:
            e = entry_from_lines(lines)
            if e["text"]:
                entries.append(e)
    if not entries:
        raise ListError("There are no texts in %s yet.\n"
                        "  A text is just the words; a blank line starts the\n"
                        "  next one. Add some with:  %s --add-texts %s"
                        % (path, PROG, path))
    return entries


def write_texts(path, entries):
    """Write a texts file: [label], the words, then a blank line."""
    header = [
        "# A list of texts for the logo maker.",
        "# Run it with:   %s --bundle NAME --texts %s"
        % (PROG, os.path.basename(path)),
        "#",
        "# Each text starts with its [label] - that becomes the file name.",
        "# One line = one picture; several lines = one picture with several",
        "# lines. A blank line starts the next text.",
        "",
    ]
    body = []
    for e in entries:
        body.append("[%s]" % e["label"])
        body.extend(e["text"].split("\n"))
        body.append("")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(header + body) + "\n")


def unique_labels(entries):
    """File-safe, unique versions of every entry's label."""
    used = {}
    names = []
    for e in entries:
        base = safe_name(e["label"])
        name, n = base, 2
        while name.lower() in used:
            name = "%s-%d" % (base, n)
            n += 1
        used[name.lower()] = True
        names.append(name)
    return names


def read_texts_batch():
    """Read several texts at once. Blank line starts a new one; a lone . ends."""
    say()
    say("  Type your texts below.")
    say("  Start with [label] to choose the file name; otherwise")
    say("  the first line becomes it.")
    say("  Press Enter for a new line in the SAME picture.")
    say("  A blank line starts a NEW picture.")
    say("  Type a single . on its own line when you are done.")
    say("  (A line that should be just a dot: type \\.)")
    say()
    lines = []
    while True:
        try:
            line = input("  > ")
        except (EOFError, KeyboardInterrupt):
            break
        if line.strip() == ".":
            break
        lines.append(unescape_line(line))
    say()

    blocks, block = [], []
    for ln in lines:
        if ln.strip():
            block.append(ln)
        elif block:
            blocks.append(block)
            block = []
    if block:
        blocks.append(block)
    out = []
    for b in blocks:
        e = entry_from_lines(b)
        if e["text"]:
            out.append(e)
    return out


def add_texts_interactive(path):
    """Add texts to a file, several at once, from the app."""
    existing = []
    if os.path.exists(path):
        try:
            existing = read_texts(path)
        except ListError:
            say("I couldn't read %s, so I'll start fresh." % path)
            existing = []
    say()
    say("  Add texts to %s" % path)
    new = read_texts_batch()
    if not new:
        say("  Nothing added.")
        return
    write_texts(path, existing + new)
    say("  Saved %d new text%s:"
        % (len(new), "" if len(new) == 1 else "s"))
    for e in new:
        say("    %-16s %s" % (e["label"], e["text"].split("\n")[0]))
    say("  Draw them:  %s --bundle NAME --texts %s" % (PROG, path))
    say()


# ------------------------------------------------------------ text editing

EDITORS = ("nvim", "vim", "vi", "nano", "micro", "emacs",
           "l3afpad", "leafpad", "gedit", "notepad")


def find_editor():
    """The user's editor: $VISUAL, then $EDITOR, then a known one."""
    for var in ("VISUAL", "EDITOR"):
        v = os.environ.get(var)
        if v:
            return shlex.split(v)
    for cand in EDITORS:
        found = shutil.which(cand)
        if found:
            return [found]
    return None


def open_in_editor(path):
    cmd = find_editor()
    if not cmd:
        say("  I couldn't find an editor (nvim, nano, micro, ...).")
        say("  Install one, or set your own:  export EDITOR=nano")
        return False
    try:
        subprocess.call(cmd + [path])
    except OSError as e:
        say("  I couldn't start %s (%s)." % (cmd[0], e))
        return False
    return True


def edit_text_in_editor(text):
    """Open one text in the user's editor; returns the new words, or None."""
    fd, tmp = tempfile.mkstemp(suffix=".txt", prefix="logo-text-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")

        def readback():
            with open(tmp, "r", encoding="utf-8") as fh:
                return fh.read()

        before = readback()
        if not open_in_editor(tmp):
            return None
        content = readback()
        if content == before:  # perhaps a windowed editor is still open
            try:
                input("  Save & close the editor, then press Enter... ")
                content = readback()
            except (EOFError, KeyboardInterrupt):
                pass
        new = content.replace("\r\n", "\n").strip("\n")
        return new or None
    finally:
        try:
            os.unlink(tmp)
        except OSError:
            pass


def create_texts_file(target):
    try:
        with open(target, "w", encoding="utf-8") as fh:
            fh.write(TEXT_TEMPLATE % {"p": PROG, "f": os.path.basename(target)})
    except OSError as e:
        say("I couldn't write %s (%s)." % (target, e.strerror))
        return False
    return True


TEXT_TEMPLATE = """\
# A list of texts for the logo maker.
# Run it with:   ./%(p)s --bundle NAME --texts %(f)s
#
# Each text starts with its [label] - that becomes the file name.
# One line = one picture; several lines = one picture with several
# lines. A blank line starts the next text.
#
# A bundle (--bundle) supplies the style, colors, size and padding,
# so all you write here are the words. Edit this file from the app:
#     ./%(p)s --edit-texts

[whatsapp]
N

[poster]
GOOD
NIGHT

OK
"""


def cmd_new_texts(target):
    if os.path.exists(target):
        say("%s already exists, so I left it alone." % target)
        say("Add to it with:  %s --add-texts %s" % (PROG, target))
        raise SystemExit(1)
    if not create_texts_file(target):
        raise SystemExit(1)
    say("Created %s with three example texts inside." % target)
    say("Add more easily with:  %s --add-texts %s" % (PROG, target))
    say("Edit it anytime with:  %s --edit-texts %s" % (PROG, target))


# ----------------------------------------------------------------- bundles

def find_bundle(name, bundles):
    for b in bundles:
        if b.get("name", "").lower() == name.lower():
            return b
    return None


def bundle_colors(bundle):
    """The effective (text, background) color names of a bundle."""
    f, b = THEME_MAP.get(bundle.get("colors", DEFAULT_THEME), (None, None))
    f = bundle.get("text-color") or f
    b = bundle.get("background") or b
    return f, b


def color_swatch(name):
    """A colored block for a color name; a blank one if unparseable."""
    try:
        return swatch(parse_color(name))
    except ValueError:
        return swatch(None)


def default_bundle(a):
    """A synthetic bundle built from the command-line flags."""
    return {
        "name": "default",
        "style": a.style or DEFAULT_STYLE,
        "colors": a.colors or DEFAULT_THEME,
        "text-color": a.text_color,
        "background": a.background,
        "size": a.size if a.size is not None else 640,
        "padding": a.padding if a.padding is not None else 14,
    }


def pick_colors():
    """Ask for a palette (reversed ones included) or C for your own.

    Returns (theme_key, custom_text_color, custom_background).
    """
    show_colors(full=False)
    default_num = next(i for i, t in enumerate(THEMES, 1)
                       if t[0] == DEFAULT_THEME)
    while True:
        ans = ask("Number, or C to mix your own colors",
                  str(default_num)).strip().lower()
        if ans == "c":
            fg_in = ask("Text color (name or #hex)", "white")
            bg_in = ask("Background color (name, #hex, or none)", "ink")
            try:
                parse_color(fg_in)
                parse_color(bg_in)
            except ValueError:
                say("  I don't recognise one of those; names like gold,")
                say("  hex like #c0392b, or none for transparent.")
                continue
            return DEFAULT_THEME, fg_in, bg_in
        if ans.isdigit() and 1 <= int(ans) <= len(THEMES):
            return THEMES[int(ans) - 1][0], None, None
        say("  Pick a number from 1 to %d, or C." % len(THEMES))


def bundle_wizard(name):
    """Ask for a look (no words), one setting at a time."""
    say()
    say("  New bundle: %s" % name)
    say("  A bundle is a look - style, colors, size, padding - no words.")
    show_styles()
    style = pick("Choose a style", STYLES, DEFAULT_STYLE)
    say("  -> %s" % STYLE_LABEL[style])
    theme, custom_fg, custom_bg = pick_colors()
    while True:
        raw = ask("Size in pixels", "640")
        try:
            size = int(raw)
            if 16 <= size <= 4096:
                break
            say("  Pick a number between 16 and 4096.")
        except ValueError:
            say("  Please type a number.")
    while True:
        raw = ask("Padding (14 is a good default, 0 to 40)", "14")
        try:
            padding = int(raw)
            if 0 <= padding <= 40:
                break
            say("  Pick a number between 0 and 40.")
        except ValueError:
            say("  Please type a number.")
    return {"name": name, "style": style, "colors": theme,
            "text-color": custom_fg, "background": custom_bg,
            "size": size, "padding": padding}


def make_bundle(name, a):
    """Build a bundle from flags, or ask interactively if none were given."""
    explicit = bool(a.style or a.colors or a.text_color
                    or a.background is not None)
    if explicit:
        return {
            "name": name,
            "style": a.style or DEFAULT_STYLE,
            "colors": a.colors or DEFAULT_THEME,
            "text-color": a.text_color,
            "background": a.background,
            "size": a.size if a.size is not None else 640,
            "padding": a.padding if a.padding is not None else 14,
        }
    return bundle_wizard(name)


def cmd_new_bundle(a):
    name = (a.new_bundle or "").strip()
    if not name:
        name = ask("Name this bundle (e.g. retro, flat)", "")
        if not name:
            say("Cancelled - nothing was saved.")
            raise SystemExit(0)
    bundles = load_bundles()
    if find_bundle(name, bundles):
        say('You already have a bundle called "%s".' % name)
        raise SystemExit(1)
    bundle = make_bundle(name, a)
    try:
        bundle_to_settings(bundle, "the bundle [%s]" % name)
    except ListError as e:
        say()
        say(str(e))
        raise SystemExit(1)
    bundles.append(bundle)
    save_bundles(bundles)
    say()
    say('Saved bundle "%s".' % name)
    say("Use it:  %s --bundle %s --texts texts.txt"
        % (PROG, safe_name(name)))
    say()


def show_bundles():
    bundles = load_bundles()
    say()
    if not bundles:
        say("You have no bundles yet.")
        say("Make one:  %s --new-bundle NAME" % PROG)
        say()
        return
    say("Your bundles:")
    say()
    for i, b in enumerate(bundles, 1):
        name = b.get("name", "?")
        s_label = STYLE_LABEL.get(b.get("style", DEFAULT_STYLE),
                                  b.get("style", "?"))
        theme = b.get("colors", DEFAULT_THEME)
        say("  %2d.  %-14s %-18s %s" % (i, name, s_label, theme))
    say()
    say("Use one:  %s --bundle NAME --texts texts.txt" % PROG)
    say("Or mix several:  %s --bundle A --bundle B --texts texts.txt"
        % PROG)
    say()


def render_entries(entries, bundles, folder):
    """Draw each text with each bundle into folder. Returns the count."""
    names = unique_labels(entries)
    many = len(bundles) > 1

    seen = {}
    plan = []
    for b in bundles:
        opts = bundle_to_settings(b, "the bundle [%s]" % b.get("name", "?"))
        for e, n in zip(entries, names):
            stem = ("%s-%s" % (safe_name(b.get("name", "bundle")), n)
                    if many else n)
            if stem in seen:
                raise ListError("Two results would be saved as %s.png.\n"
                                "  Give them different names." % stem)
            seen[stem] = True
            plan.append((stem, e["text"], opts))

    made = 0
    for stem, t, opts in plan:
        out = os.path.join(folder, stem + ".png")
        px = render(t, opts["path"], opts["size"], opts["fg"],
                    opts["bg"], opts["margin"])
        write_png(out, opts["size"], opts["size"], px)
        first = t.split("\n")[0]
        shown = first if "\n" not in t else first + " ..."
        say("  %-24s %4d x %-4d  %-14s %s"
            % (os.path.basename(out), opts["size"], opts["size"],
               opts["style"], shown))
        made += 1
    return made


def run_texts(path, bundles, out_dir=None):
    """Draw every text in a file, once per bundle."""
    entries = read_texts(path)
    folder = out_dir or os.path.join(HERE, "pictures")
    if not os.path.isabs(folder):
        folder = os.path.join(HERE, folder)

    os.makedirs(folder, exist_ok=True)
    say()
    say("Reading %s - %d text%s."
        % (path, len(entries), "" if len(entries) == 1 else "s"))
    say("Drawing with %d bundle%s."
        % (len(bundles), "" if len(bundles) == 1 else "s"))
    say("Writing into %s" % folder)
    say()

    made = render_entries(entries, bundles, folder)

    say()
    say("Done - %d picture%s in %s"
        % (made, "" if made == 1 else "s", folder))


def draw_multi_bundle(a):
    """Draw one piece of text with several saved bundles at once."""
    text = a.text.strip()
    saved = load_bundles()
    picks = []
    for bn in a.bundle:
        b = find_bundle(bn, saved)
        if b is None:
            say("There is no bundle called %r." % bn)
            say("Run  %s --bundles  to see them." % PROG)
            raise SystemExit(1)
        picks.append(b)

    folder = os.path.join(HERE, "pictures")
    prefix = None
    if a.save:
        if os.path.isdir(a.save):
            folder = a.save
        else:
            prefix = os.path.splitext(os.path.basename(a.save))[0]
            folder = os.path.dirname(os.path.abspath(a.save))

    safe = "".join(c if c.isalnum() else "_" for c in text) or "letters"
    plan = []
    for b in picks:
        name = b.get("name", "bundle")
        item = {
            "style": a.style or b.get("style", DEFAULT_STYLE),
            "colors": a.colors or b.get("colors", DEFAULT_THEME),
            "fg": a.text_color or b.get("text-color"),
            "bg": (a.background if a.background is not None
                   else b.get("background")),
            "size": a.size if a.size is not None else b.get("size", 640),
            "padding": (a.padding if a.padding is not None
                        else b.get("padding", 14)),
        }
        try:
            opts = look_settings(item, "the bundle [%s]" % name)
        except ListError as e:
            say()
            say(str(e))
            raise SystemExit(1)
        plan.append(("%s-%s" % (safe_name(prefix or name), safe), opts))

    seen = {}
    for stem, _ in plan:
        if stem in seen:
            say("Two results would be saved as %s.png." % stem)
            say("Give your bundles different names.")
            raise SystemExit(1)
        seen[stem] = True

    os.makedirs(folder, exist_ok=True)
    say()
    say('Drawing "%s" with %d looks.' % (text.split("\n")[0], len(plan)))
    say("Writing into %s" % folder)
    say()

    made = 0
    for stem, opts in plan:
        out = unique_path(folder, stem)
        try:
            px = render(text, opts["path"], opts["size"], opts["fg"],
                        opts["bg"], opts["margin"])
        except SystemExit:
            say('  Skipped "%s": nothing drew with style %s.'
                % (stem, opts["style"]))
            continue
        write_png(out, opts["size"], opts["size"], px)
        if not a.no_preview:
            preview(px, opts["size"])
        shown = text.split("\n")[0]
        say("  %-24s %4d x %-4d  %-14s %s"
            % (os.path.basename(out), opts["size"], opts["size"],
               opts["style"],
               shown if "\n" not in text else shown + " ..."))
        made += 1

    say()
    say("Done - %d picture%s in %s"
        % (made, "" if made == 1 else "s", folder))


def compare_variants(what):
    """The looks --compare should draw: (category, key) pairs."""
    pairs = []
    if what in ("styles", "both"):
        pairs += [("style", k) for k, _, _ in STYLES]
    if what in ("colors", "both"):
        for k in THEME_MAP:
            f_name = THEME_MAP[k][0]
            try:
                visible = parse_color(f_name) is not None
            except ValueError:
                visible = False
            if visible:
                pairs.append(("color", k))
    return pairs


def cmd_compare(what, text, out_dir=None, show_preview=True):
    """Draw one text against many looks so the best one is easy to spot."""
    if what not in ("styles", "colors", "both"):
        say("I can only compare 'styles', 'colors', or 'both'.")
        raise SystemExit(1)
    folder = out_dir or os.path.join(HERE, "pictures", "compare")
    if not os.path.isabs(folder):
        folder = os.path.join(HERE, folder)

    plan, skipped = [], []
    for category, key in compare_variants(what):
        if category == "style":
            item = {"style": key, "colors": "classic"}
        else:
            item = {"style": DEFAULT_STYLE, "colors": key}
        try:
            opts = look_settings(item,
                                 "the comparison %s '%s'" % (category, key))
        except ListError as e:
            say()
            say(str(e))
            raise SystemExit(1)
        if not os.path.exists(opts["path"]):
            skipped.append(key)
            continue
        plan.append((category, key, opts))

    if not plan:
        say("Nothing to compare - none of those looks has a font I can use.")
        raise SystemExit(1)

    os.makedirs(folder, exist_ok=True)
    say()
    say('Comparing looks for "%s" - %d variation%s.'
        % (quote(text), len(plan), "" if len(plan) == 1 else "s"))
    say("Writing into %s" % folder)
    say()

    made = 0
    for category, key, opts in plan:
        out = unique_path(folder, "%s-%s" % (category, safe_name(key)))
        try:
            px = render(text, opts["path"], opts["size"], opts["fg"],
                        opts["bg"], opts["margin"])
        except SystemExit:
            say('  Skipped %s %s: nothing drew there.' % (category, key))
            continue
        write_png(out, opts["size"], opts["size"], px)
        if show_preview:
            preview(px, opts["size"], cols=24)
        say("  %-24s %4d x %-4d  %-14s %s"
            % (os.path.basename(out), opts["size"], opts["size"],
               opts["style"], key))
        made += 1

    say()
    say("Done - %d picture%s in %s"
        % (made, "" if made == 1 else "s", folder))
    if skipped:
        say("(left out, font file missing: %s)" % ", ".join(skipped))


# -------------------------------------------------------------- friendly shell

def supports_color():
    return sys.stdout.isatty() and os.environ.get("TERM", "") != "dumb"


def say(msg=""):
    print(msg)


def swatch(rgba):
    """A small colored block, so a color name is visible, not just spelled."""
    if not supports_color():
        return "  "
    if rgba is None:
        return "\x1b[90m::\x1b[0m"
    return "\x1b[48;2;%d;%d;%dm  \x1b[0m" % rgba[:3]


def preview(px, size, cols=34):
    """Draw the finished image in the terminal using half-block characters."""
    if not supports_color():
        return
    rows = cols  # the image is square, and one block cell covers two rows
    step = size / float(cols)
    lines = []
    for ry in range(0, rows, 2):
        line = ""
        for rx in range(cols):
            def sample(r):
                y = min(size - 1, int((r + 0.5) * step))
                x = min(size - 1, int((rx + 0.5) * step))
                i = (y * size + x) * 4
                r_, g_, b_, a_ = px[i], px[i + 1], px[i + 2], px[i + 3]
                if a_ < 255:  # show transparency against a mid grey
                    t = a_ / 255.0
                    r_ = int(r_ * t + 128 * (1 - t))
                    g_ = int(g_ * t + 128 * (1 - t))
                    b_ = int(b_ * t + 128 * (1 - t))
                return r_, g_, b_
            top, bot = sample(ry), sample(ry + 1)
            line += ("\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm\u2580"
                     % (top + bot))
        lines.append("   " + line + "\x1b[0m")
    say()
    say("\n".join(lines))
    say()


def show_styles():
    say()
    say("Lettering styles:")
    say()
    for i, (key, label, _) in enumerate(STYLES, 1):
        star = " (default)" if key == DEFAULT_STYLE else ""
        say("  %2d. %-14s %s%s" % (i, key, label, star))
    say()


def show_colors(full=True):
    say()
    say("Color pairs:")
    say()
    for i, (key, label, f, b) in enumerate(THEMES, 1):
        star = " (default)" if key == DEFAULT_THEME else ""
        say("  %2d. %s%s %-13s %-32s text=%s bg=%s%s"
            % (i, swatch(parse_color(b)), swatch(parse_color(f)), key,
               label, f, b, star))
    say()
    if not full:
        return
    say("Or set your own with --text-color and --background,")
    say("using a name (%s, ...)" % ", ".join(sorted(NAMED)[:6]))
    say("or a hex code like #c0392b. Use 'none' for a transparent background.")
    say()


def ask(prompt, default=None):
    suffix = " [%s]: " % default if default is not None else ": "
    try:
        answer = input(prompt + suffix).strip()
    except (EOFError, KeyboardInterrupt):
        say()
        say("Cancelled - nothing was saved.")
        raise SystemExit(0)
    return answer or (default if default is not None else "")


def pick(prompt, options, default_key):
    """Let the user answer with a number or a name."""
    keys = [o[0] for o in options]
    while True:
        answer = ask(prompt, default_key).lower()
        if answer.isdigit() and 1 <= int(answer) <= len(keys):
            return keys[int(answer) - 1]
        if answer in keys:
            return answer
        say("  Please enter a number from 1 to %d, or one of the names above."
            % len(keys))


# ------------------------------------------------------------- interactive menu

def pick_number(prompt, count, default=1):
    """Ask for a number from 1 to count. Returns the number."""
    while True:
        answer = ask(prompt, str(default))
        if answer.isdigit() and 1 <= int(answer) <= count:
            return int(answer)
        say("  Pick a number from 1 to %d." % count)


def menu_pick_style():
    """Show each style with a rendered sample.  Arrow keys or Enter to advance."""
    say()
    say("  Browse styles")
    say("  Press Enter to see the next one, Q to go back.")
    say()

    total = len(STYLES)
    idx = 0
    while idx < total:
        key, label, _ = STYLES[idx]
        font_path = os.path.join(FONTDIR, STYLE_FILE[key])
        if not os.path.exists(font_path):
            idx += 1
            continue
        say("  Style %d of %d:  %s" % (idx + 1, total, label))
        say("  (%s)" % key)
        try:
            px = render("Ag", font_path, 320, (255, 255, 255, 255),
                        (20, 22, 26, 255), 0.14)
            preview(px, 320, cols=24)
        except SystemExit:
            say("  (no preview available for this style)")
        try:
            answer = input("  Enter = next, Q = back: ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            say()
            return
        if answer in ("q", "quit", "exit"):
            return
        idx += 1
    say()
    say("  That was the last style.  Back to the menu.")
    say()


def default_look():
    return {"name": "default", "style": DEFAULT_STYLE,
            "colors": DEFAULT_THEME, "text-color": None, "background": None,
            "size": 640, "padding": 14}


def pick_bundle():
    """Choose a saved bundle, or fall back to the default look."""
    bundles = load_bundles()
    if not bundles:
        say("  (No bundles yet - using the default look.)")
        return default_look()
    say()
    say("  Pick a look:")
    say()
    for i, b in enumerate(bundles, 1):
        s_label = STYLE_LABEL.get(b.get("style", DEFAULT_STYLE),
                                  b.get("style", "?"))
        say("    %2d.  %-14s %s" % (i, b.get("name", "?"), s_label))
    say("     %2d.  %s" % (len(bundles) + 1, "Default look"))
    say()
    n = pick_number("  Number", len(bundles) + 1, len(bundles) + 1)
    if n <= len(bundles):
        return bundles[n - 1]
    return default_look()


def menu_my_texts():
    """Show every labeled text and let the user draw, edit or remove one."""
    path = texts_file()
    try:
        entries = read_texts(path)
    except ListError:
        say()
        say("  You don't have any texts yet.")
        say("  Add some with option 1, then they show up here.")
        say()
        return

    say()
    say("  Your texts:")
    say()
    for i, e in enumerate(entries, 1):
        first = e["text"].split("\n")[0]
        if len(first) > 20:
            first = first[:18] + ".."
        say("    %2d.  %-16s %s" % (i, e["label"], first))
    say()
    say("    Pick a number, or Enter to go back.")

    ans = ask("  Number", "")
    if not ans:
        return
    if not ans.isdigit() or not (1 <= int(ans) <= len(entries)):
        say("  That's not a valid number.")
        return
    idx = int(ans) - 1
    entry = entries[idx]

    say()
    say('  [%s]  "%s"'
        % (entry["label"], entry["text"].replace("\n", " / ")))
    say()
    say("    1.  Draw it")
    say("    2.  Edit it here")
    say("    3.  Edit it in my editor")
    say("    4.  Remove it")
    say("    5.  Back")
    say()
    choice = ask("  Number", "1")
    if choice == "1":
        bundle = pick_bundle()
        folder = os.path.join(HERE, "pictures")
        os.makedirs(folder, exist_ok=True)
        say()
        say("  Drawing ...")
        try:
            render_entries([entry], [bundle], folder)
        except SystemExit:
            say("  Sorry, that text doesn't work with that look.")
        say()
    elif choice == "2":
        say()
        say('  Type the new words for [%s].' % entry["label"])
        say("  Enter = new line in the SAME picture. A lone . finishes.")
        lines = []
        while True:
            try:
                line = input("  > ")
            except (EOFError, KeyboardInterrupt):
                break
            if line.strip() == ".":
                break
            lines.append(unescape_line(line))
        while lines and not lines[-1].strip():
            lines.pop()
        text = "\n".join(lines)
        if not text.strip():
            say("  Kept the old words.")
        else:
            entry["text"] = text
            write_texts(path, entries)
            say("  Updated [%s]." % entry["label"])
        say()
    elif choice == "3":
        new = edit_text_in_editor(entry["text"])
        if new is None:
            say("  Kept the old words.")
        else:
            entry["text"] = new
            write_texts(path, entries)
            say("  Updated [%s]." % entry["label"])
        say()
    elif choice == "4":
        entries.pop(idx)
        write_texts(path, entries)
        say("  Removed [%s]." % entry["label"])
        say()


def menu_edit_texts_file(path=None):
    """Open a texts file (default texts.txt) in the user's own editor."""
    path = path or texts_file()
    if not os.path.exists(path):
        if create_texts_file(path):
            say("  Created %s with examples inside." % path)
    open_in_editor(path)
    try:
        n = len(read_texts(path))
        say()
        say("  %d text%s ready in %s." % (n, "" if n == 1 else "s", path))
    except ListError as e:
        say()
        say(str(e))
    say()


def menu_draw_all():
    """Draw every text with one look."""
    path = texts_file()
    try:
        read_texts(path)
    except ListError:
        say()
        say("  You don't have any texts yet - add some with option 1 first.")
        say()
        return
    bundle = pick_bundle()
    try:
        run_texts(path, [bundle])
    except ListError as e:
        say()
        say(str(e))
    except OSError as e:
        say("Something went wrong while saving (%s)." % e.strerror)


def menu_add_texts():
    """Add texts to the shared texts file, several at once."""
    add_texts_interactive(texts_file())


def menu_edit_bundle(bundles, idx):
    """Tweak one saved bundle; shows a fresh picture after every change."""
    while True:
        b = bundles[idx]
        name = b.get("name", "?")
        s_label = STYLE_LABEL.get(b.get("style", DEFAULT_STYLE),
                                  b.get("style", "?"))
        f_name, b_name = bundle_colors(b)
        say()
        say('  [%s]  %s  %s%s' % (name, s_label,
                                  color_swatch(f_name),
                                  color_swatch(b_name)))
        say("  text=%s  bg=%s" % (f_name, b_name))
        try:
            opts = bundle_to_settings(b, "the bundle [%s]" % name)
            px = render("Ag", opts["path"], 240, opts["fg"], opts["bg"],
                        opts["margin"])
            preview(px, 240, cols=22)
        except SystemExit:
            say("  (no preview available for this style)")
        say("    1.  Change the colors")
        say("    2.  Change the style")
        say("    3.  Change size or padding")
        say("    4.  Rename it")
        say("    5.  Delete it")
        say("    6.  Done")
        say()
        choice = ask("  Number", "6").strip()
        if choice == "1":
            theme, cfg, cbg = pick_colors()
            b["colors"] = theme
            b["text-color"] = cfg
            b["background"] = cbg
            save_bundles(bundles)
        elif choice == "2":
            say()
            current = STYLES.index(b.get("style")) + 1 \
                if b.get("style") in STYLE_FILE else 1
            for i, (key, label, _) in enumerate(STYLES, 1):
                marker = " <--" if key == b.get("style") else ""
                say("    %2d.  %s%s" % (i, label, marker))
            n = pick_number("  Number", len(STYLES), current)
            b["style"] = STYLES[n - 1][0]
            save_bundles(bundles)
        elif choice == "3":
            while True:
                raw = ask("  Size in pixels", str(b.get("size", 640)))
                try:
                    v = int(raw)
                    if 16 <= v <= 4096:
                        b["size"] = v
                        break
                    say("  Pick a number between 16 and 4096.")
                except ValueError:
                    say("  Please type a number.")
            while True:
                raw = ask("  Padding (0 to 40)", str(b.get("padding", 14)))
                try:
                    v = int(raw)
                    if 0 <= v <= 40:
                        b["padding"] = v
                        break
                    say("  Pick a number between 0 and 40.")
                except ValueError:
                    say("  Please type a number.")
            save_bundles(bundles)
        elif choice == "4":
            new = ask("  New name", name).strip()
            if not new or new.lower() == name.lower():
                continue
            clash = find_bundle(new, bundles)
            if clash and clash is not b:
                say('  You already have a bundle called "%s".' % new)
                continue
            b["name"] = new
            save_bundles(bundles)
            say('  Renamed to "%s".' % new)
        elif choice == "5":
            try:
                confirm = input('  Delete "%s"?  Type yes to confirm: '
                                % name)
            except (EOFError, KeyboardInterrupt):
                say()
                return
            if confirm.strip().lower() == "yes":
                bundles.pop(idx)
                save_bundles(bundles)
                say("  Deleted.")
                say()
                return
            say("  Kept it.")
        else:
            return


def menu_bundles():
    """Show saved bundles: pick one to tweak it, or make a new one."""
    while True:
        bundles = load_bundles()
        say()
        say("  Bundles (a look: style, colors, size, padding - no words)")
        say()
        if not bundles:
            say("    You have no bundles yet.")
        else:
            for i, b in enumerate(bundles, 1):
                s_label = STYLE_LABEL.get(b.get("style", DEFAULT_STYLE),
                                          b.get("style", "?"))
                f, bg = bundle_colors(b)
                say("    %2d.  %s%s %-12s %-22s %s"
                    % (i, color_swatch(bg), color_swatch(f),
                       b.get("name", "?"), s_label,
                       b.get("colors", DEFAULT_THEME)))
        say()
        say("    N.  New bundle")
        say("    B.  Back")
        say()

        choice = ask("  Number to edit, N, or B", "B").strip().lower()
        if not choice or choice in ("b", "back"):
            return
        if choice in ("n", "new"):
            name = ask("  Bundle name", "")
            if not name:
                continue
            if find_bundle(name, bundles):
                say('  You already have a bundle called "%s".' % name)
                continue
            bundle = bundle_wizard(name)
            try:
                bundle_to_settings(bundle, "the bundle [%s]" % name)
            except ListError as e:
                say()
                say(str(e))
                continue
            bundles.append(bundle)
            save_bundles(bundles)
            say()
            say('  Saved bundle "%s".' % name)
            say()
            continue
        if choice.isdigit() and 1 <= int(choice) <= len(bundles):
            menu_edit_bundle(bundles, int(choice) - 1)
            continue
        say("  Pick a bundle number, N, or B.")


def menu_texts():
    """Everything about the words: add, review, edit."""
    while True:
        say()
        say("  Texts")
        say()
        say("    1.  Add texts")
        say("    2.  My texts")
        say("    3.  Edit texts file")
        say("    4.  Back")
        say()
        choice = ask("  Pick a number", "").strip().lower()
        if choice == "1":
            menu_add_texts()
        elif choice == "2":
            menu_my_texts()
        elif choice == "3":
            menu_edit_texts_file()
        elif choice in ("4", "b", "back"):
            return
        else:
            say("  Pick a number from 1 to 4.")


def menu_main():
    """The main interactive menu.  Shown when you run the app with no arguments."""
    while True:
        say()
        say("  +---------------------------------------+")
        say("  |           Logo Creator                |")
        say("  +---------------------------------------+")
        say()
        say("    1.  Texts")
        say("    2.  Bundles")
        say("    3.  Draw all texts")
        say("    4.  Browse styles")
        say("    5.  Quit")
        say()

        choice = ask("  Pick a number", "1")

        if choice == "1":
            menu_texts()
        elif choice == "2":
            menu_bundles()
        elif choice == "3":
            menu_draw_all()
        elif choice == "4":
            menu_pick_style()
        elif choice == "5":
            say()
            say("  See you next time!")
            say()
            return
        else:
            say("  Pick a number from 1 to 5.")


PROG = ("./logo" if os.path.exists(os.path.join(HERE, "logo"))
        else os.path.basename(sys.argv[0]))

HELP_EPILOG = """
examples:
  %(p)s                       ask me step by step (easiest)
  %(p)s A                     one letter, using the default look
  %(p)s "JD" --style modern --colors crimson
  %(p)s N --text-color gold --background slate
  %(p)s A --background none --save logo.png
  %(p)s --compare both AB   draw AB in every style and color pair
  %(p)s --new-list            create an example list file to edit
  %(p)s --from logos.txt      draw every logo in that file at once
  %(p)s --styles              show every lettering style
  %(p)s --colors-list         show every color pair and color name

bundles (a saved look: style, colors, size, padding - no words):
  %(p)s --new-bundle retro    save a look, asking step by step
  %(p)s --bundles             list your saved bundles

texts (just the words, drawn with a bundle):
  %(p)s --new-texts           create a texts.txt to edit
  %(p)s --add-texts texts.txt add several texts at once
  %(p)s --edit-texts          open texts.txt in your own editor
  %(p)s --bundle retro --texts texts.txt          draw every text
  %(p)s --bundle retro --bundle flat --texts texts.txt   try both looks

The picture is always square and saved as a PNG with no quality loss.
""" % {"p": PROG}


def main():
    ap = argparse.ArgumentParser(
        prog=PROG,
        description="Turn letters into a square picture "
                    "for a profile photo, logo, or chat icon.",
        epilog=HELP_EPILOG,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("text", nargs="?",
                    help="the letter or letters to draw, e.g. A or \"JD\"")
    ap.add_argument("-s", "--style", default=None, metavar="NAME",
                    help="lettering style; see --styles (default: %s)"
                         % DEFAULT_STYLE)
    ap.add_argument("-c", "--colors", default=None, metavar="NAME",
                    help="a ready-made color pair; see --colors-list "
                         "(default: %s)" % DEFAULT_THEME)
    ap.add_argument("--text-color", "--fg", default=None, metavar="COLOR",
                    help="color of the letters, e.g. gold or #c8a34a")
    ap.add_argument("--background", "--bg", default=None, metavar="COLOR",
                    help="color behind the letters, or 'none' for transparent")
    ap.add_argument("--size", type=int, default=None, metavar="PIXELS",
                    help="width and height in pixels (default: 640, "
                         "the size chat apps like)")
    ap.add_argument("--padding", type=int, default=None, metavar="PERCENT",
                    help="empty space around the letters, 0-40 "
                         "(default: 14)")
    ap.add_argument("-o", "--save", default=None, metavar="FILE",
                    help="where to save the picture; a folder that already "
                         "exists receives it under an automatic name "
                         "(default: the pictures folder)")
    ap.add_argument("--font-file", default=None, metavar="FILE",
                    help="use your own .otf or .ttf file instead of a style")
    ap.add_argument("--from", dest="from_file", metavar="FILE",
                    help="draw every logo listed in a file, all at once")
    ap.add_argument("--out-dir", metavar="FOLDER",
                    help="folder for --from/--texts pictures (overrides the file)")
    ap.add_argument("--new-list", metavar="FILE", nargs="?", const="logos.txt",
                    help="write a ready-to-edit example list "
                         "(default: logos.txt)")
    ap.add_argument("-b", "--bundle", action="append", default=None,
                    metavar="NAME",
                    help="a saved bundle to use; repeat it to try several "
                         "looks at once (see --bundles)")
    ap.add_argument("--compare", nargs="?", choices=("styles", "colors",
                                                     "both"),
                    const="both", metavar="WHAT",
                    help="draw the text once per lettering style and/or "
                         "color pair (styles, colors, or both) and save "
                         "them side by side to pick from")
    ap.add_argument("--bundles", action="store_true",
                    help="list your saved bundles and exit")
    ap.add_argument("--new-bundle", default=None, metavar="NAME",
                    help="save a look as a bundle (no words); asks if you "
                         "leave out --style/--colors")
    ap.add_argument("--texts", default=None, metavar="FILE",
                    help="draw every text in this file, using --bundle")
    ap.add_argument("--new-texts", metavar="FILE", nargs="?",
                    const="texts.txt", default=None,
                    help="write a ready-to-edit texts file "
                         "(default: texts.txt)")
    ap.add_argument("--add-texts", default=None, metavar="FILE",
                    help="add texts to a file, several at once")
    ap.add_argument("--edit-texts", metavar="FILE", nargs="?", const="",
                    default=None,
                    help="open a texts file in your editor "
                         "(default: texts.txt)")
    ap.add_argument("--styles", action="store_true",
                    help="list the lettering styles and exit")
    ap.add_argument("--colors-list", action="store_true",
                    help="list the color pairs and color names and exit")
    ap.add_argument("--no-preview", action="store_true",
                    help="skip the preview drawn in the terminal")
    a = ap.parse_args()

    if a.styles:
        show_styles()
        return
    if a.colors_list:
        show_colors()
        return

    if a.bundles:
        show_bundles()
        return

    if a.new_bundle:
        cmd_new_bundle(a)
        return

    if a.new_texts:
        cmd_new_texts(a.new_texts)
        return

    if a.add_texts:
        add_texts_interactive(a.add_texts)
        return

    if a.edit_texts is not None:
        menu_edit_texts_file(a.edit_texts or None)
        return

    if a.compare:
        cmd_compare(a.compare, a.text or "Ag", a.out_dir,
                    show_preview=not a.no_preview)
        return

    if a.texts:
        bundles = []
        if a.bundle:
            saved = load_bundles()
            for bn in a.bundle:
                b = find_bundle(bn, saved)
                if b is None:
                    say("There is no bundle called %r." % bn)
                    say("Run  %s --bundles  to see them." % PROG)
                    raise SystemExit(1)
                bundles.append(b)
        else:
            bundles = [default_bundle(a)]
        try:
            run_texts(a.texts, bundles, a.out_dir)
        except ListError as e:
            say()
            say(str(e))
            raise SystemExit(1)
        except OSError as e:
            say("Something went wrong while saving (%s)." % e.strerror)
            raise SystemExit(1)
        return

    if a.new_list:
        target = a.new_list
        if os.path.exists(target):
            say("%s already exists, so I left it alone." % target)
            say("Choose another name, for example:  ./logo --new-list "
                "my-logos.txt")
            raise SystemExit(1)
        try:
            with open(target, "w", encoding="utf-8") as fh:
                fh.write(EXAMPLE_LIST)
        except OSError as e:
            say("I couldn't write %s (%s)." % (target, e.strerror))
            raise SystemExit(1)
        say("Created %s with four example logos inside." % target)
        say("Open it in any text editor, change it however you like, then:")
        say("  ./logo --from %s" % target)
        return

    if a.from_file:
        try:
            run_list(a.from_file, a.out_dir)
        except ListError as e:
            say()
            say(str(e))
            raise SystemExit(1)
        except OSError as e:
            say("Something went wrong while saving (%s)." % e.strerror)
            raise SystemExit(1)
        return

    interactive = False
    if a.text is None and a.font_file is None and not a.bundle:
        menu_main()
        return
    else:
        if not a.text:
            say("Please tell me what to draw, for example:  %s A"
                % PROG)
            raise SystemExit(1)
        text, out, font_file = a.text, a.save, a.font_file

        style = a.style
        size = a.size
        theme = a.colors
        fg_name = a.text_color
        bg_name = a.background
        padding = a.padding

        if a.bundle:
            if len(a.bundle) > 1:
                draw_multi_bundle(a)
                return
            saved = load_bundles()
            b = find_bundle(a.bundle[0], saved)
            if b is None:
                say("There is no bundle called %r." % a.bundle[0])
                say("Run  %s --bundles  to see them." % PROG)
                raise SystemExit(1)
            if style is None:
                style = b.get("style")
            if size is None:
                size = b.get("size", 640)
            if theme is None:
                theme = b.get("colors")
            if fg_name is None:
                fg_name = b.get("text-color")
            if bg_name is None:
                bg_name = b.get("background")
            if padding is None:
                padding = b.get("padding", 14)

        style = style or DEFAULT_STYLE
        size = size if size is not None else 640
        theme = theme or DEFAULT_THEME
        padding = padding if padding is not None else 14

        if theme not in THEME_MAP:
            say("There is no color pair called %r." % theme)
            say("Run  %s --colors-list  to see the choices."
                % PROG)
            raise SystemExit(1)
        fg_name = fg_name or THEME_MAP[theme][0]
        bg_name = bg_name if bg_name is not None else THEME_MAP[theme][1]
        margin = padding / 100.0

    # ------------------------------------------------ check everything first
    if font_file:
        path = font_file
        style_label = os.path.basename(font_file)
        if not os.path.exists(path):
            say("I can't find that font file: %s" % path)
            raise SystemExit(1)
    else:
        if style not in STYLE_FILE:
            say("There is no style called %r." % style)
            say("Run  %s --styles  to see the choices."
                % PROG)
            raise SystemExit(1)
        path = os.path.join(FONTDIR, STYLE_FILE[style])
        style_label = STYLE_LABEL[style]
        if not os.path.exists(path):
            say("The font for the %s style is missing from:" % style)
            say("  %s" % path)
            raise SystemExit(1)

    if size < 16:
        say("The size needs to be at least 16 pixels. 640 is a good choice.")
        raise SystemExit(1)
    if size > 4096:
        say("That size is very large; please choose 4096 pixels or less.")
        raise SystemExit(1)
    if not 0 <= margin <= 0.4:
        say("Padding should be between 0 and 40 percent.")
        raise SystemExit(1)

    fg = color_or_quit(fg_name, "text")
    bg = color_or_quit(bg_name, "background")
    if fg is None:
        say("The letters need a visible color; 'none' only works for the "
            "background.")
        raise SystemExit(1)

    if out and os.path.isdir(out):
        safe = "".join(c if c.isalnum() else "_" for c in text) or "letters"
        out = unique_path(out, "%s-%s"
                          % (safe, style if not font_file else "custom"))
    elif not out:
        safe = "".join(c if c.isalnum() else "_" for c in text) or "letters"
        out = unique_path(os.path.join(HERE, "pictures"),
                          "%s-%s" % (safe, style if not font_file else "custom"))
    folder = os.path.dirname(os.path.abspath(out))
    try:
        os.makedirs(folder, exist_ok=True)
    except OSError as e:
        say("I couldn't create the folder %s (%s)." % (folder, e.strerror))
        raise SystemExit(1)

    # ------------------------------------------------------------- do the work
    if interactive:
        say()
        say("Drawing ...")
    try:
        px = render(text, path, size, fg, bg, margin)
    except SystemExit:
        say("None of those characters exist in the %s style." % style_label)
        say("Try a different style with --style, or --styles to see them all.")
        raise SystemExit(1)

    try:
        write_png(out, size, size, px)
    except OSError as e:
        say("I couldn't save the picture (%s)." % e.strerror)
        raise SystemExit(1)

    if not a.no_preview:
        preview(px, size)
    say("Saved: %s" % out)
    say("       %d x %d pixels, %s style, ready to use as a picture."
        % (size, size, style_label))
    if interactive:
        say()
        say("Tip: next time you can go straight there with")
        say("  %s %s --style %s --colors %s"
            % (PROG, quote(text), style, theme))


def quote(s):
    return s if s.isalnum() else '"%s"' % s


def unique_path(folder, stem):
    """Never overwrite a picture the user already made."""
    candidate = os.path.join(folder, stem + ".png")
    n = 2
    while os.path.exists(candidate):
        candidate = os.path.join(folder, "%s-%d.png" % (stem, n))
        n += 1
    return candidate


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        say()
        say("Cancelled - nothing was saved.")
        raise SystemExit(0)
