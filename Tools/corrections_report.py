#!/usr/bin/env python3
"""Prehled oprav po fotce (~/Music/Rack Memory/Corrections/corrections.jsonl) a kandidati na testovaci pravdu.

Pouziti:  python3 Tools/corrections_report.py ["~/Music/Rack Memory"] [--write]

- rozlisi sile dukazu:  user-corrected (uzivatel hodnotu zmenil)  >  user-individual (potvrdil jeden ovladac)
                        >  user-bulk ("Potvrdit vse" - NENI overena pravda)
  Starsi zaznamy (do 0.12, "verification": "user") se zaradi podle casu: vic nez 5 potvrzeni
  ve stejne minute = hromadne potvrzeni (bulk).
- spocita, jak casto mela fotka pravdu a o kolik se mylila (u oprav), podle modelu a ovladace
- overene pripady (corrected / individual) prida k fotce v Tests/photo_dataset/catalog.json jako
  "userTruth" kandidaty -- jen s --write, a jen jako navrh (truth se NEmeni; zmenit ji je rucni krok po kontrole).
Nic se neuci samo a nic se neposila.
"""
import json, os, sys, collections, hashlib

args = [a for a in sys.argv[1:] if not a.startswith('--')]
root = os.path.expanduser(args[0] if args else '~/Music/Rack Memory')
write = '--write' in sys.argv
here = os.path.dirname(os.path.abspath(__file__))
catalog_path = os.path.join(here, '..', 'Tests', 'photo_dataset', 'catalog.json')
profiles = {p['id']: p for p in json.load(open(os.path.join(here, '..', 'Assets', 'profiles.json')))['profiles']}

path = os.path.join(root, 'Corrections', 'corrections.jsonl')
rows = [json.loads(l) for l in open(path, encoding='utf-8') if l.strip()] if os.path.exists(path) else []
per_minute = collections.Counter((r.get('photoFingerprint'), r.get('time', '')[:16], r.get('status')) for r in rows)

def strength(r):
    v = r.get('verification', 'user')
    if v in ('user-corrected', 'user-individual', 'user-bulk'): return v
    if r.get('status') == 'corrected': return 'user-corrected'
    return 'user-bulk' if per_minute[(r.get('photoFingerprint'), r.get('time', '')[:16], r.get('status'))] > 5 else 'user-individual'

def degrees(r):
    prof = profiles.get(r.get('model')); c = None
    if prof: c = next((c for c in prof['controls'] if c['id'] == r.get('control')), None)
    if not c or r.get('predicted') is None: return None
    t = c.get('type', 'knob')
    if t == 'knob':
        span = c.get('angleMax', 150) - c.get('angleMin', -150)
        return abs(r['predicted'] - r['value']) * span
    if t == 'stepped':
        span = c.get('angleMax', 150) - c.get('angleMin', -150); steps = max(2, c.get('steps', 2))
        return abs(r['predicted'] - r['value']) * span / (steps - 1)
    return None

by = collections.defaultdict(lambda: collections.Counter())
errors = collections.defaultdict(list)
for r in rows:
    s = strength(r); r['_strength'] = s
    key = r.get('model', '?')
    by[key][s] += 1
    by[key][r.get('status')] += 1
    d = degrees(r)
    if r.get('status') == 'corrected' and d is not None: errors[(key, r.get('control'))].append(d)

print(f"Zaznamu: {len(rows)}  ({path})")
for m, c in sorted(by.items()):
    print(f"  {m:24} potvrzeno {c['confirmed']:3}  opraveno {c['corrected']:3}   | overeno: opravy {c['user-corrected']}, jednotlive {c['user-individual']}  | hromadne (neovereno) {c['user-bulk']}")
if errors:
    print("Opravy (o kolik se fotka mylila):")
    for (m, ctl), e in sorted(errors.items()):
        print(f"  {m:24} {ctl:14} n={len(e)}  prumer {sum(e)/len(e):5.1f} st.  max {max(e):5.1f} st.")
verified = [r for r in rows if r['_strength'] in ('user-corrected', 'user-individual')]
print(f"Overene pripady (smi do testovaci pravdy po kontrole): {len(verified)}; hromadne potvrzene (NE): {sum(1 for r in rows if r['_strength'] == 'user-bulk')}")

if write and os.path.exists(catalog_path):
    cat = json.load(open(catalog_path, encoding='utf-8'))
    by_md5 = {p.get('md5'): p for p in cat['photos'] if p.get('md5')}
    added = 0
    for r in verified:
        p = by_md5.get(r.get('photoFingerprint'))
        if not p: continue
        ut = p.setdefault('userTruth', {"note": "kandidati z oprav uzivatele - zkontrolovat a teprve pak prevest do truth", "devices": {}})
        dev = ut['devices'].setdefault(r['model'], {})
        dev[r['control']] = {"value": r['value'], "verification": r['_strength'], "time": r.get('time')}
        added += 1
    json.dump(cat, open(catalog_path, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    print(f"Do katalogu pridano {added} kandidatu (userTruth).")
