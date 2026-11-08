#!/usr/bin/env python3
"""Render a recorded frame (frame_NNNN.txt) to PNG, imitating Tiny3D + libfont."""
import sys, os, struct
import numpy as np
from PIL import Image, ImageDraw

def col(h):
    v = int(h, 16); return ((v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255)

def main(frame_path, out_png, glyph_path):
    W, H = 1920, 1080
    img = np.zeros((H, W, 3), np.float32)
    glyphs = np.fromfile(glyph_path, np.uint8).reshape(256, 64, 64).astype(np.float32) / 255.0
    cover_cache = {}

    def blend(x0, y0, rgba_patch):
        h, w = rgba_patch.shape[:2]
        x1, y1 = x0 + w, y0 + h
        cx0, cy0, cx1, cy1 = max(0, x0), max(0, y0), min(W, x1), min(H, y1)
        if cx0 >= cx1 or cy0 >= cy1: return
        p = rgba_patch[cy0 - y0:cy1 - y0, cx0 - x0:cx1 - x0]
        a = p[..., 3:4]
        img[cy0:cy1, cx0:cx1] = img[cy0:cy1, cx0:cx1] * (1 - a) + p[..., :3] * a

    def quad(vs, tex):
        xs = [v[0] for v in vs]; ys = [v[1] for v in vs]
        x0, y0, x1, y1 = int(np.floor(min(xs))), int(np.floor(min(ys))), int(np.ceil(max(xs))), int(np.ceil(max(ys)))
        w, h = max(1, x1 - x0), max(1, y1 - y0)
        mask = Image.new('L', (w, h), 0)
        ImageDraw.Draw(mask).polygon([(v[0] - x0, v[1] - y0) for v in vs], fill=255)
        m = np.asarray(mask, np.float32)[..., None] / 255.0
        cs = np.array([col(v[2]) for v in vs], np.float32) / 255.0
        axis = abs(vs[0][1] - vs[1][1]) < 0.01 and abs(vs[1][0] - vs[2][0]) < 0.01
        if axis and len(vs) == 4:   # bilinear vertex colours
            gy = np.linspace(0, 1, h)[:, None, None]; gx = np.linspace(0, 1, w)[None, :, None]
            top = cs[0] * (1 - gx) + cs[1] * gx; bot = cs[3] * (1 - gx) + cs[2] * gx
            c = top * (1 - gy) + bot * gy
        else:
            c = np.ones((h, w, 4), np.float32) * cs.mean(0)
        if tex is not None and tex >= 100000:   # cover stand-in: draw a labelled image
            key = (tex, w, h)
            if key not in cover_cache:
                hue = (tex * 47) % 360
                im = Image.new('RGB', (w, h), 'hsl(%d,55%%,40%%)' % hue)
                d = ImageDraw.Draw(im)
                for k in range(0, w + h, 18): d.line([(k, 0), (k - h, h)], fill='hsl(%d,55%%,48%%)' % hue, width=6)
                d.rectangle([0, 0, w - 1, int(h * 0.09)], fill=(235, 235, 235))
                d.text((8, h - 24), 'COVER', fill=(255, 255, 255))
                cover_cache[key] = np.asarray(im, np.float32) / 255.0
            rgb = cover_cache[key]
            c = np.concatenate([rgb * c[..., :3], c[..., 3:4]], axis=2)
        c = c.copy(); c[..., 3:4] *= m
        blend(x0, y0, c)

    def char(code, x, y, sx, sy, color):
        g = glyphs[code]
        w, h = max(1, int(round(sx))), max(1, int(round(sy)))
        gi = np.asarray(Image.fromarray((g * 255).astype(np.uint8)).resize((w, h), Image.BILINEAR), np.float32) / 255.0
        c = np.array(col(color), np.float32) / 255.0
        patch = np.ones((h, w, 4), np.float32) * c
        patch[..., 3] *= gi
        blend(int(round(x)), int(round(y)), patch)

    verts = []
    for line in open(frame_path):
        p = line.split()
        if not p: continue
        if p[0] == 'CLEAR':
            v = int(p[1], 16); img[:] = np.array([(v >> 16) & 255, (v >> 8) & 255, v & 255], np.float32) / 255.0
        elif p[0] == 'POLY': verts = []
        elif p[0] == 'V': verts.append((float(p[1]), float(p[2]), p[3], float(p[4]), float(p[5])))
        elif p[0] == 'END':
            tex = int(p[1]); tex = tex if tex >= 0 else None
            for i in range(0, len(verts) - 3, 4): quad(verts[i:i + 4], tex)
        elif p[0] == 'CHAR':
            char(int(p[1]), float(p[2]), float(p[3]), int(p[4]), int(p[5]), p[6])
    Image.fromarray(np.clip(img * 255, 0, 255).astype(np.uint8)).save(out_png)

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3])
