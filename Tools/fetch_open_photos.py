#!/usr/bin/env python3
"""Rack Memory - sber fotek hardwaru POUZE se svobodnou licenci, ktera dovoluje komercni pouziti.

    python3 Tools/fetch_open_photos.py                 # vsechny modely z Assets/profiles.json
    python3 Tools/fetch_open_photos.py ua.1176ln ua.la2a
    python3 Tools/fetch_open_photos.py --max 40        # max. fotek na model (vychozi 25)

Zdroje (oficialni API, zadne obchazeni, zadne prihlasovani):
  - Wikimedia Commons (commons.wikimedia.org/w/api.php)
  - Openverse (api.openverse.org) - katalog CC fotek (vcetne Flickru), filtr na komercni pouziti
Bere jen licence: CC0, Public Domain Mark, CC BY, CC BY-SA. NC (nekomercni) a ND (bez upravy) se NEBEROU.
Instagram, Reverb, obchody, foto vyrobcu ani "napsal, ze mu to je jedno" se NEBEROU - to neni licence.

Vystup (jen u tebe na disku, do gitu ani do pluginu se fotky nedavaji):
  ~/Music/Rack Memory/Dataset/open/<model>/<zdroj>_<id>.jpg
  ~/Music/Rack Memory/Dataset/open/sources.jsonl   - autor, licence, odkaz, uvedeni autora (attribution)
Pak: Claude fotky projde, vyradi nerelevantni, u pouzitelnych zapise pravdu (polohy ovladacu) do katalogu
a zaradi je do testu rozpoznavani. Nic se nesiri dal; CC BY / BY-SA vyzaduje uvest autora -> je v sources.jsonl.
"""
import json, os, sys, time, hashlib, urllib.request, urllib.parse, re

UA = "RackMemoryDatasetBot/0.1 (+https://github.com/DusiXxx/MEMORY-RACK; open-license photos only)"
OK_LICENSES = {"cc0", "pdm", "by", "by-sa"}
ROOT = os.path.expanduser("~/Music/Rack Memory/Dataset/open")
HERE = os.path.dirname(os.path.abspath(__file__))
MIN_SIDE = 700

# vyhledavaci dotazy: nazev z profilu + bezne prezdivky
EXTRA = {
    "ua.1176ln": ["1176LN", "Universal Audio 1176", "1176 compressor", "1176 limiting amplifier"],
    "ua.la2a": ["LA-2A", "Teletronix LA-2A", "LA2A compressor"],
    "empiricallabs.distressor": ["Distressor", "Empirical Labs Distressor", "EL8 Distressor"],
    "api.2500plus": ["API 2500", "API 2500 compressor"],
    "warmaudio.eqpwa": ["EQP-WA", "Warm Audio EQP", "Pultec EQP-1A"],
    "warmaudio.wa76": ["WA76", "Warm Audio WA76"],
    "warmaudio.wa2a": ["WA-2A", "Warm Audio WA-2A"],
    "api.512c": ["API 512c", "API 512"],
    "amsneve.1073lb": ["Neve 1073", "1073LB", "Neve 1073 preamp"],
    "elysia.xfilter.1u": ["elysia xfilter"], "elysia.xfilter.500": ["xfilter 500"],
    "elysia.xfilter.me": ["xfilter mastering edition"], "elysia.xmax.500": ["elysia xmax"],
    "elysia.nvelope.500": ["elysia nvelope"], "spl.vitalizer.mk3t": ["SPL Vitalizer"],
    "wesaudio.rhea": ["WesAudio Rhea"], "cranborne.camden500": ["Cranborne Camden 500"],
}
GENERIC = ["studio outboard rack", "recording studio rack gear", "500 series lunchbox", "mastering studio outboard"]

def get_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read().decode("utf-8"))

def get_bytes(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()

def norm_license(s):
    s = (s or "").lower().replace("cc ", "cc-").strip()
    if s in ("cc0", "cc-zero", "cc0 1.0", "public domain", "pdm", "pd"): return "cc0" if "0" in s else "pdm"
    if s.startswith("public domain"): return "pdm"
    m = re.match(r"cc[- ]?(by(?:-sa)?|by-nc.*|by-nd.*)(?:[- ]?\d.*)?$", s)
    if m: return m.group(1)
    if s in ("by", "by-sa"): return s
    return s

def commons(query, limit):
    """Wikimedia Commons: soubory (namespace 6) + licence z extmetadata."""
    q = urllib.parse.urlencode({
        "action": "query", "format": "json", "generator": "search", "gsrnamespace": 6, "gsrlimit": min(50, limit * 2),
        "gsrsearch": query + " filetype:bitmap", "prop": "imageinfo", "iiprop": "url|size|extmetadata|sha1",
        "iiurlwidth": 2400})
    d = get_json("https://commons.wikimedia.org/w/api.php?" + q)
    for p in (d.get("query", {}).get("pages", {}) or {}).values():
        ii = (p.get("imageinfo") or [{}])[0]
        md = ii.get("extmetadata", {})
        lic = norm_license(md.get("LicenseShortName", {}).get("value", ""))
        artist = re.sub(r"<[^>]+>", "", md.get("Artist", {}).get("value", "")).strip()
        yield dict(source="commons", id=str(p.get("pageid")), title=p.get("title", ""), license=lic,
                   licenseText=md.get("LicenseShortName", {}).get("value", ""), licenseUrl=md.get("LicenseUrl", {}).get("value", ""),
                   author=artist, landing=ii.get("descriptionurl", ""), url=ii.get("thumburl") or ii.get("url"),
                   width=ii.get("width", 0), height=ii.get("height", 0))

def openverse(query, limit):
    """Openverse: jen licence povolujici komercni pouziti a upravy."""
    q = urllib.parse.urlencode({"q": query, "license": "cc0,pdm,by,by-sa", "page_size": min(50, limit * 2), "mature": "false"})
    d = get_json("https://api.openverse.org/v1/images/?" + q)
    for r in d.get("results", []):
        yield dict(source="openverse-" + (r.get("source") or "?"), id=r.get("id", ""), title=r.get("title", ""),
                   license=norm_license(r.get("license", "")), licenseText=(r.get("license", "") + " " + (r.get("license_version") or "")).strip(),
                   licenseUrl=r.get("license_url", ""), author=r.get("creator") or "", landing=r.get("foreign_landing_url", ""),
                   url=r.get("url"), width=r.get("width") or 0, height=r.get("height") or 0,
                   attribution=r.get("attribution", ""))

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    mx = 25
    if "--max" in sys.argv:
        mx = int(sys.argv[sys.argv.index("--max") + 1]); args = [a for a in args if a != str(mx)]
    profiles = json.load(open(os.path.join(HERE, "..", "Assets", "profiles.json"), encoding="utf-8"))["profiles"]
    want = [p for p in profiles if not args or p["id"] in args]
    if not args: want.append({"id": "_generic", "name": ""})
    os.makedirs(ROOT, exist_ok=True)
    log_path = os.path.join(ROOT, "sources.jsonl")
    seen = set()
    if os.path.exists(log_path):
        for l in open(log_path, encoding="utf-8"):
            try: seen.add(json.loads(l)["md5"])
            except Exception: pass
    log = open(log_path, "a", encoding="utf-8")
    total = 0
    for p in want:
        queries = GENERIC if p["id"] == "_generic" else ([p["name"]] + EXTRA.get(p["id"], []))
        got = 0
        for query in queries:
            for fn in (commons, openverse):
                if got >= mx: break
                try: items = list(fn(query, mx))
                except Exception as e:
                    print(f"  ! {fn.__name__} '{query}': {e}"); continue
                time.sleep(1.0)
                for it in items:
                    if got >= mx: break
                    if it["license"] not in OK_LICENSES or not it.get("url"): continue
                    if min(it.get("width") or 0, it.get("height") or 0) and min(it["width"], it["height"]) < MIN_SIDE: continue
                    try: data = get_bytes(it["url"])
                    except Exception as e:
                        print(f"  ! stazeni {it['url']}: {e}"); continue
                    time.sleep(0.5)
                    md5 = hashlib.md5(data).hexdigest()
                    if md5 in seen: continue
                    seen.add(md5)
                    ext = ".png" if data[:4] == b"\x89PNG" else ".jpg"
                    d = os.path.join(ROOT, p["id"]); os.makedirs(d, exist_ok=True)
                    fn_ = re.sub(r"[^A-Za-z0-9_.-]", "_", f"{it['source']}_{it['id']}")[:80] + ext
                    open(os.path.join(d, fn_), "wb").write(data)
                    it.update(model=p["id"], query=query, file=os.path.join(p["id"], fn_), md5=md5, bytes=len(data),
                              fetched=time.strftime("%Y-%m-%d"), status="neprohlednuto")
                    if not it.get("attribution"):
                        it["attribution"] = f"\"{it['title']}\" – {it['author'] or 'neznámý autor'}, {it['licenseText']} ({it['landing']})"
                    log.write(json.dumps(it, ensure_ascii=False) + "\n"); log.flush()
                    got += 1; total += 1
                    print(f"  + {p['id']:26} {it['license']:6} {it['source']:22} {fn_}")
        print(f"{p['id']}: {got} fotek")
    print(f"Hotovo: {total} novych fotek v {ROOT}  (seznam s licencemi: sources.jsonl)")

if __name__ == "__main__":
    main()
