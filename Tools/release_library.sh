#!/bin/bash
# MEMORY RACK - vyda KNIHOVNU HARDWARU (profily pristroju), oddelene od pluginu.
# Plugin ji najde v ••• → Zkontrolovat knihovnu hardwaru, overi SHA-256 a nainstaluje do ~/Music/Rack Memory/Hardware.
#
# Pouziti:  bash Tools/release_library.sh <verze-knihovny> "Co je noveho" [slozka]
#   slozka (vychozi Tools/hardware-library) obsahuje:  library.json  +  images/*.jpg|png
#   library.json: {"schema":1, "libraryVersion": N, "profiles":[ ... stejny format jako Assets/profiles.json ... ]}
# Pravidla: ID pristroju a ovladacu se nikdy nemeni; upraveny profil musi mit vyssi profileVersion
# a zachovat vsechna ID ovladacu (jinak ho plugin neprevezme).
set -e
cd "$(dirname "$0")/.."
VER="$1"; NOTES="${2:-Knihovna hardwaru $1}"; SRC="${3:-Tools/hardware-library}"
REPO="${RM_UPDATE_REPO:-DusiXxx/MEMORY-RACK}"
[ -n "$VER" ] || { echo "Pouziti: bash Tools/release_library.sh <cislo> \"co je noveho\""; exit 1; }
[ -f "$SRC/library.json" ] || { echo "CHYBA: chybi $SRC/library.json"; exit 1; }
python3 - "$SRC/library.json" "$VER" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert d.get('schema') == 1, 'schema musi byt 1'
assert d.get('libraryVersion') == int(sys.argv[2]), 'libraryVersion v library.json musi byt ' + sys.argv[2]
for p in d['profiles']:
    ids = [c['id'] for c in p.get('controls', [])]
    assert len(ids) == len(set(ids)), p['id']
print('library.json OK:', len(d['profiles']), 'profilu')
PY
OUT="$HOME/Downloads/MemoryRack-hardware-$VER"; rm -rf "$OUT"; mkdir -p "$OUT"
( cd "$SRC" && zip -qr "$OUT/hardware-$VER.zip" library.json images 2>/dev/null || zip -qr "$OUT/hardware-$VER.zip" library.json )
SHA=$(shasum -a 256 "$OUT/hardware-$VER.zip" | awk '{print $1}')
SIZE=$(stat -f%z "$OUT/hardware-$VER.zip" 2>/dev/null || stat -c%s "$OUT/hardware-$VER.zip")
python3 - "$OUT/hardware.json" <<PY
import json, sys
json.dump({"schema": 1, "libraryVersion": int("$VER"), "minimum_plugin": "0.9.0",
           "download": "https://github.com/$REPO/releases/download/hardware/hardware-$VER.zip",
           "sha256": "$SHA", "size": int("$SIZE"), "changes": ["""$NOTES"""]}, open(sys.argv[1], "w"), indent=2)
PY
echo "==> Pripraveno v $OUT"; ls -1 "$OUT"
if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then
    gh release view hardware --repo "$REPO" >/dev/null 2>&1 || gh release create hardware --repo "$REPO" --title "Knihovna hardwaru" --notes "Profily přístrojů pro MEMORY RACK" --latest=false
    gh release upload hardware "$OUT/hardware-$VER.zip" "$OUT/hardware.json" --repo "$REPO" --clobber
    echo "OK: knihovna v$VER zverejnena."
else
    echo "Bez gh: otevri https://github.com/$REPO/releases/tag/hardware (pri prvnim vydani: New release, Tag 'hardware',"
    echo "NEOZNACUJ jako Latest), nahraj OBA soubory ze slozky $OUT (stary hardware.json nahrad)."
fi
