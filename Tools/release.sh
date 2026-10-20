#!/bin/bash
# MEMORY RACK - vyda novou verzi na GitHub Releases. Plugin si ji pak sam najde, stahne a nainstaluje.
#
# Pouziti:   bash Tools/release.sh ["Co je noveho"]                 (repozitar DusiXxx/MEMORY-RACK)
#            bash Tools/release.sh <ucet>/<repozitar> ["Co je noveho"]
#            bash Tools/release.sh --verify [ucet/repozitar] [verze]
#
# Co udela:
#   1. postavi plugin (build.sh: univerzalni Intel + Apple Silicon, auval) s adresou vydani zabudovanou v pluginu
#   2. zabali ho do RackMemory-<verze>-mac.zip, spocita SHA-256, velikost a architektury
#   3. napise latest.json (to, co plugin cte)
#   4. s prihlasenym GitHub CLI (gh) vytvori vydani v<verze>; jinak vypise presny postup pres web
set -e
cd "$(dirname "$0")/.."
DEFAULT_REPO="DusiXxx/MEMORY-RACK"

if [ "$1" = "--verify" ]; then
    REPO="${2:-$DEFAULT_REPO}"
    VER="${3:-$(sed -n 's/^project(RackMemory VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)}"
    URL="https://github.com/$REPO/releases/latest/download/latest.json"
    echo "==> Overuji $URL"
    GOT=$(curl -fsSL "$URL") || { echo "CHYBA: latest.json neni dostupny (repozitar verejny? vydani zverejnene, ne pre-release?)"; exit 1; }
    echo "$GOT" | grep -q "\"version\": \"$VER\"" && echo "OK: GitHub nabizi verzi $VER - pluginy se aktualizuji." \
        || { echo "POZOR: GitHub nabizi jinou verzi:"; echo "$GOT" | head -5; exit 1; }
    exit 0
fi

if [ -n "$1" ] && echo "$1" | grep -q "/"; then REPO="$1"; shift; else REPO="$DEFAULT_REPO"; fi
NOTES="${1:-}"
[ "$(uname)" = "Darwin" ] || { echo "Tenhle skript je pro macOS."; exit 1; }

VERSION=$(sed -n 's/^project(RackMemory VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
TAG="v$VERSION"
[ -z "$NOTES" ] && NOTES="MEMORY RACK $VERSION"
echo "==> Vydani MEMORY RACK $VERSION do github.com/$REPO"

HAVE_GH=0
if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then HAVE_GH=1; fi
if [ $HAVE_GH = 1 ] && gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
    echo "CHYBA: vydani $TAG uz existuje. Zvys verzi v CMakeLists.txt: project(RackMemory VERSION ...)"; exit 1
fi

# ---- 1. build (adresa vydani se zapece do pluginu)
RM_ARCHS="arm64;x86_64" RM_UPDATE_REPO="$REPO" bash build.sh
COMP="$HOME/Library/Audio/Plug-Ins/Components/Rack Memory.component"
[ -d "$COMP" ] || { echo "CHYBA: plugin neni postaveny"; exit 1; }
INSTALLED=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$COMP/Contents/Info.plist" 2>/dev/null || echo "?")
[ "$INSTALLED" = "$VERSION" ] || { echo "CHYBA: postaveny plugin ma verzi $INSTALLED, ocekavam $VERSION"; exit 1; }

# ---- 2. balicek
OUT="$HOME/Downloads/MemoryRack-release-$VERSION"
rm -rf "$OUT"; mkdir -p "$OUT"
ASSET="RackMemory-$VERSION-mac.zip"
STAGE=$(mktemp -d)
ditto "$COMP" "$STAGE/Rack Memory.component"
cp "JAK NAINSTALOVAT.txt" "$STAGE/" 2>/dev/null || true
ditto -c -k --sequesterRsrc "$STAGE" "$OUT/$ASSET"
rm -rf "$STAGE"
SHA=$(shasum -a 256 "$OUT/$ASSET" | awk '{print $1}')
SIZE=$(stat -f%z "$OUT/$ASSET")
ARCHS=$(lipo -archs "$COMP/Contents/MacOS/Rack Memory" 2>/dev/null || echo "x86_64")
ARCH_JSON=$(for a in $ARCHS; do printf '"%s", ' "$a"; done | sed 's/, $//')
echo "    SHA-256: $SHA"
echo "    velikost: $SIZE B, architektury: $ARCHS"

# ---- 3. latest.json
python3 - "$OUT/latest.json" <<PY
import json, sys
data = {
  "version": "$VERSION",
  "channel": "stable",
  "minimum_version": "0.8.0",
  "notes": """$NOTES""",
  "notes_url": "https://github.com/$REPO/releases/tag/$TAG",
  "download": "https://github.com/$REPO/releases/download/$TAG/$ASSET",
  "sha256": "$SHA", "size": int("$SIZE"),
  "architectures": [$ARCH_JSON], "formats": ["AU"], "macos_min": "10.13"
}
json.dump(data, open(sys.argv[1], "w"), indent=2)
PY
printf '%s\n' "$NOTES" > "$OUT/RELEASE_NOTES.md"
echo "==> Pripraveno v $OUT:"; ls -1 "$OUT"

# ---- 4. zverejneni
if [ $HAVE_GH = 1 ]; then
    read -r -p "Zverejnit $TAG na github.com/$REPO? [a/N] " OK
    if [ "$OK" = "a" ] || [ "$OK" = "A" ] || [ "$OK" = "y" ]; then
        gh release create "$TAG" "$OUT/$ASSET" "$OUT/latest.json" --repo "$REPO" \
           --title "MEMORY RACK $VERSION" --notes-file "$OUT/RELEASE_NOTES.md" --latest
        bash "$0" --verify "$REPO" "$VERSION"
    else
        echo "Nezverejneno. Soubory zustavaji v $OUT."
    fi
else
    echo
    echo "GitHub CLI (gh) neni prihlasene - zverejni rucne (2 minuty):"
    echo "  1. otevri https://github.com/$REPO/releases/new"
    echo "  2. Tag: $TAG   Title: MEMORY RACK $VERSION"
    echo "  3. pretahni do okna OBA soubory ze slozky $OUT:  $ASSET  a  latest.json"
    echo "  4. NEZASKRTAVEJ 'Set as a pre-release', nech 'Set as the latest release', klikni Publish release"
    echo "  5. pak spust:  bash Tools/release.sh --verify"
    open "https://github.com/$REPO/releases/new" 2>/dev/null || true
    open "$OUT" 2>/dev/null || true
fi
