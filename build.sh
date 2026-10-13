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

JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
# Univerzalni plugin (Intel i Apple Silicon), aby fungoval i na novejsich Macich.
ARCHS="${RM_ARCHS:-arm64;x86_64}"
build () {
  "$CMAKE" -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=$1" \
           "-DRM_UPDATE_REPO=${RM_UPDATE_REPO:-DusiXxx/MEMORY-RACK}" $JUCE_ARG &&
  "$CMAKE" --build build --target RackMemory_AU -j "$JOBS"
}
if ! build "$ARCHS"; then
  echo; echo "Univerzalni build nesel, zkousim jen pro tento Mac ($(uname -m))..."
  build "$(uname -m)"
fi

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
PKG="$HOME/Downloads/Rack Memory $VER.zip"
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
