#!/usr/bin/env python3
"""Sestavi lokalni sadu fotek (Tests/photo_dataset/local) z tvych Stazenych - fotky nejsou v gitu (autorska prava).

Pouziti:  python3 Tools/photo_dataset_from_downloads.py [~/Downloads]
Kopiruje jen soubory uvedene v catalog.json ("originalName"), zkontroluje SHA-256 a pak vytvori ztizene varianty.
Pak:      build/RackMemoryTests photoset Tests/photo_dataset/catalog.json Tests/photo_dataset/local
"""
import json, os, sys, hashlib, subprocess
from PIL import Image
src = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/Downloads')
root = os.path.join(os.path.dirname(__file__), '..', 'Tests', 'photo_dataset')
cat = json.load(open(os.path.join(root, 'catalog.json')))
local = os.path.join(root, 'local'); os.makedirs(local, exist_ok=True)
ok = bad = missing = 0
for p in cat['photos']:
    if p.get('variantOf') or not p.get('originalName'): continue
    s = os.path.join(src, p['originalName']); d = os.path.join(local, p['file'])
    if not os.path.exists(s): missing += 1; continue
    im = Image.open(s)
    if p['file'].endswith('.png') and p['originalName'].endswith('.webp'): im.convert('RGB').save(d)
    else: open(d, 'wb').write(open(s, 'rb').read())
    if hashlib.sha256(open(d, 'rb').read()).hexdigest() == p['sha256']: ok += 1
    else: bad += 1; print('jiny obsah nez v katalogu:', p['file'])
print(f'zkopirovano {ok}, jiny obsah {bad}, chybi {missing}')
subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'make_photo_variants.py'), root])
