#!/usr/bin/env python3
"""Ztizene varianty testovacich fotek (tma, rozmazani, mensi rozliseni, sikmy pohled, silna komprese).

Pouziti:  python3 Tools/make_photo_variants.py Tests/photo_dataset
Vezme fotky z catalog.json s "use": "test" a overenou pravdou (verification eye/verified), vytvori varianty
v local/variants/ a zapise je do catalog.json jako dalsi polozky (condition = typ varianty, pravda se dedi:
jas, ostrost, rozliseni ani natoceni fotky nemeni polohu knobu). Varianty se negeneruji z fotek bez pravdy.
"""
import json, os, sys, hashlib, random
from PIL import Image, ImageFilter, ImageEnhance

root = sys.argv[1] if len(sys.argv) > 1 else 'Tests/photo_dataset'
cat_path = os.path.join(root, 'catalog.json')
cat = json.load(open(cat_path))
local = os.path.join(root, 'local'); out = os.path.join(local, 'variants'); os.makedirs(out, exist_ok=True)
base = [p for p in cat['photos'] if p.get('use') == 'test' and not p.get('variantOf')
        and p.get('truth', {}).get('verification') in ('eye', 'verified')]
cat['photos'] = [p for p in cat['photos'] if not p.get('variantOf')]

def perspective(im, k):
    w, h = im.size
    # pohled zboku: prava strana mensi (koeficienty pro Image.transform PERSPECTIVE, mapuji vystup -> vstup)
    import numpy as np
    src = [(0, 0), (w, 0), (w, h), (0, h)]
    dst = [(0, 0), (w, h * k), (w, h * (1 - k)), (0, h)]
    A = []; B = []
    for (x, y), (u, v) in zip(dst, src):
        A += [[x, y, 1, 0, 0, 0, -u * x, -u * y], [0, 0, 0, x, y, 1, -v * x, -v * y]]; B += [u, v]
    coeffs = np.linalg.solve(np.array(A, float), np.array(B, float))
    return im.transform((w, h), Image.PERSPECTIVE, tuple(coeffs), Image.BICUBIC, fillcolor=(30, 30, 30))

variants = {
    'tma':        lambda im: ImageEnhance.Brightness(im).enhance(0.30),
    'rozmazana':  lambda im: im.filter(ImageFilter.GaussianBlur(max(1.5, max(im.size) / 1100))),
    'polovicni':  lambda im: im.resize((im.width // 2, im.height // 2), Image.LANCZOS),
    'ctvrtinova': lambda im: im.resize((im.width // 4, im.height // 4), Image.LANCZOS),
    'zboku':      lambda im: perspective(im, 0.10),
    'jpeg30':     None,
}
n = 0
for p in base:
    im = Image.open(os.path.join(local, p['file'])).convert('RGB')
    for name, fn in variants.items():
        fname = f"{p['id']}__{name}.jpg"
        path = os.path.join(out, fname)
        (im if fn is None else fn(im)).save(path, quality=30 if name == 'jpeg30' else 90)
        v = dict(p); v.update(id=f"{p['id']}__{name}", file='variants/' + fname, variantOf=p['id'], condition=name,
                             sha256=hashlib.sha256(open(path, 'rb').read()).hexdigest(),
                             notes=f"odvozeno z {p['id']} ({name}); pravda zdedena")
        cat['photos'].append(v); n += 1
json.dump(cat, open(cat_path, 'w'), indent=1, ensure_ascii=False)
print(f"{len(base)} fotek s pravdou -> {n} variant v {out}")
