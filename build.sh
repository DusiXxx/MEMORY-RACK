#!/bin/bash
# Rack Memory - sestaveni a instalace AU pro Logic Pro (macOS, staci Command Line Tools + CMake)
# Vysledek: plugin nainstalovany v ~/Library/Audio/Plug-Ins/Components
#           + balicek pro kamarady ve Stazenych: "Rack Memory <verze>.zip"
set -e
cd "$(dirname "$0")"

CMAKE="$(command -v cmake || true)"
if [ -z "$CMAKE" ] && [ -x /Applications/CMake.app/Contents/bin/cmake ]; then
  CMAKE=/Applications/CMake.app/Contents/bin/cmake
fi
if [ -z "$CMAKE" ]; then
  echo "CHYBA: nenasel jsem CMake (cekam ho v /Applications/CMake.app)."; exit 1
fi

# JUCE 8.0.9 z drivejsich buildu (DeVerb / SampliX) se pouzije znovu, at se nestahuje
JUCE_ARG=""
for d in $(find "$HOME/Downloads" "$HOME/Documents" "$HOME/Desktop" -maxdepth 6 -type d -name juce-src 2>/dev/null); do
  if grep -q "VERSION 8.0.9" "$d/CMakeLists.txt" 2>/dev/null && [ -d "$d/modules/juce_core" ]; then
    JUCE_ARG="-DFETCHCONTENT_SOURCE_DIR_JUCE=$d"
    echo "Pouzivam JUCE z: $d"
    break
  fi
done
[ -z "$JUCE_ARG" ] && echo "Stahuji JUCE 8.0.9 (jen poprve, par minut)..."

# --- Setrne k pocitaci (MacBook 2017 se pri plnem buildu prehrival) ---------------------------------
# 1) Paralelnich kompilaci: o jednu min nez fyzickych jader (2017 MacBook Pro: 4 jadra -> 3, drive 8).
#    Zmena: RM_JOBS=2 bash build.sh
PHYS="$(sysctl -n hw.physicalcpu 2>/dev/null || echo 2)"
JOBS="${RM_JOBS:-$(( PHYS > 1 ? PHYS - 1 : 1 ))}"
# 2) Jen pro tento Mac (drive vzdy Intel + Apple Silicon = vse prelozit dvakrat).
#    Univerzalni plugin pro kamarady s Apple Silicon:  bash build.sh --universal   (release.sh to dela sam)
ARCHS="${RM_ARCHS:-$(uname -m)}"
[ "$1" = "--universal" ] && ARCHS="arm64;x86_64"
# 3) Slozka buildu MIMO rozbalenou slozku: "rm -rf RackMemory" pri nove verzi ji nesmaze, takze se JUCE
#    a nezmenene soubory podruhe neprekladaji (drive se pokazde prekladalo vsechno od nuly).
BUILD_DIR="${RM_BUILD_DIR:-$HOME/Library/Caches/RackMemoryBuild/$(echo "$ARCHS" | tr ';' '-')}"
mkdir -p "$BUILD_DIR"
# slozka buildu patri jine kopii zdrojaku -> zacit znovu (jinak CMake hlasi chybu)
if [ -f "$BUILD_DIR/CMakeCache.txt" ] && ! grep -q "CMAKE_HOME_DIRECTORY:INTERNAL=$(pwd)$" "$BUILD_DIR/CMakeCache.txt"; then
  rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"
fi
# rozbalene soubory maji cas ze zipu: zmenene (podle obsahu) se oznaci jako nove, aby se urcite prelozily
STAMP="$BUILD_DIR/.rm-sources.sha"
NEW="$(mktemp)"
find Source Assets CMakeLists.txt -type f -print0 2>/dev/null | sort -z | xargs -0 shasum 2>/dev/null > "$NEW" || true
if [ -f "$STAMP" ]; then
  diff "$STAMP" "$NEW" | sed -n 's/^> [0-9a-f]*  //p' | while IFS= read -r f; do [ -f "$f" ] && touch "$f"; done
else
  find Source Assets CMakeLists.txt -type f -exec touch {} + 2>/dev/null || true
fi

echo "Build: architektura $ARCHS, $JOBS kompilaci najednou, slozka $BUILD_DIR"
START=$(date +%s)
build () {
  "$CMAKE" -S . -B "$BUILD_DIR" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=$1" \
           "-DRM_UPDATE_REPO=${RM_UPDATE_REPO:-DusiXxx/MEMORY-RACK}" $JUCE_ARG &&
  nice -n 10 "$CMAKE" --build "$BUILD_DIR" --target RackMemory_AU -j "$JOBS"
}
if ! build "$ARCHS"; then
  if [ "$ARCHS" != "$(uname -m)" ]; then
    echo; echo "Univerzalni build nesel, zkousim jen pro tento Mac ($(uname -m))..."
    ARCHS="$(uname -m)"; build "$ARCHS"
  else
    exit 1
  fi
fi
mv "$NEW" "$STAMP"
echo "Build trval $(( $(date +%s) - START )) s"

COMP="$HOME/Library/Audio/Plug-Ins/Components/Rack Memory.component"
if [ ! -d "$COMP" ]; then
  echo "CHYBA: plugin se nenainstaloval do $COMP"; exit 1
fi
xattr -dr com.apple.quarantine "$COMP" 2>/dev/null || true

killall -9 AudioComponentRegistrar 2>/dev/null || true
echo
echo "Architektury: $(lipo -archs "$COMP/Contents/MacOS/Rack Memory" 2>/dev/null)"
echo "Validace (auval -v aufx Rkmm Dusx):"
if auval -v aufx Rkmm Dusx 2>&1 | tail -n 4; then :; fi

# balicek pro kamarady (posli jim tenhle jeden zip)
VER="$(sed -n 's/^project(RackMemory VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
if [ "$(lipo -archs "$COMP/Contents/MacOS/Rack Memory" 2>/dev/null | wc -w | tr -d ' ')" -ge 2 ]; then
  PKG="$HOME/Downloads/Rack Memory $VER.zip"
else
  PKG="$HOME/Downloads/Rack Memory $VER ($(uname -m)).zip"     # jen pro Mac se stejnym procesorem; univerzalni: --universal
fi
TMPD="$(mktemp -d)"
ditto "$COMP" "$TMPD/Rack Memory.component"
cp "JAK NAINSTALOVAT.txt" "$TMPD/" 2>/dev/null || true
rm -f "$PKG"
ditto -c -k --sequesterRsrc "$TMPD" "$PKG"
rm -rf "$TMPD"

echo
echo "HOTOVO. Nainstalovano: $COMP"
echo "Balicek pro kamarada: $PKG"
echo "V Logicu: Audio FX > Audio Units > DusiX > Rack Memory (po instalaci Logic restartuj)."
echo "Data se ukladaji do: ~/Music/Rack Memory"
