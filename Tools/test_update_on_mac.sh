#!/bin/bash
# Rack Memory - test aktualizace NA MACU se skutecnymi nastroji macOS (ditto, PlistBuddy, lsof, auval).
#
#   bash Tools/test_update_on_mac.sh                  # testovaci "novou" verzi vyrobi z nainstalovane (bez buildu, nehreje)
#   bash Tools/test_update_on_mac.sh <balicek.zip>    # nebo skutecny balicek z release.sh / GitHubu
#
# Postup: 1) kontrola, ze Logic nebezi  2) otisk tve knihovny (~/Music/Rack Memory: verze, presety, rack, opravy...)
#         3) instalace instalatorem pluginu  4) overeni verze + auval + zaloha  5) knihovna beze zmeny
#         6) vraceni predchozi verze (rollback)  7) knihovna beze zmeny  8) znovu nova verze (kdyz byl dan zip)
# Vysledek: ~/Music/Rack Memory/Reports/update-test-<datum>.txt (posli ho, nebo ho precte Claude).
# Testovaci verze (bez zipu) ma jen jine cislo v Info.plist - binarka je stejna; po testu se vrati puvodni.
set -u
cd "$(dirname "$0")/.."
LIB="$HOME/Music/Rack Memory"
COMPDIR="$HOME/Library/Audio/Plug-Ins/Components"
COMP="$COMPDIR/Rack Memory.component"
STATE="$LIB/Updates"
SCRIPT="Assets/rackmemory-install.sh"
REPORT="$LIB/Reports/update-test-$(date +%Y%m%d-%H%M%S).txt"
mkdir -p "$LIB/Reports" "$STATE"
PB=/usr/libexec/PlistBuddy
FAILS=0
say () { echo "$*" | tee -a "$REPORT"; }
check () { if eval "$2"; then say "  OK    $1"; else say "  CHYBA $1"; FAILS=$((FAILS + 1)); fi; }
ver () { "$PB" -c "Print :CFBundleShortVersionString" "$1/Contents/Info.plist" 2>/dev/null; }
libhash () {   # vse v knihovne krome Updates / Reports / Recovery (ty meni instalator a plugin)
    (cd "$LIB" && find . -type f ! -path "./Updates/*" ! -path "./Reports/*" ! -path "./Recovery/*" ! -name ".DS_Store" -print0 \
        | sort -z | xargs -0 shasum 2>/dev/null | shasum | cut -c1-16)
}
run_installer () {   # mode zip verze
    /bin/sh "$SCRIPT" "$1" "$2" "$3" "$COMPDIR" "$STATE" >> "$STATE/install.log" 2>&1
    sed -n 's/.*"state":"\([^"]*\)".*/\1/p' "$STATE/install-status.json"
}

say "Rack Memory - test aktualizace na Macu  $(date)"
say "macOS $(sw_vers -productVersion), $(uname -m)"
[ "$(uname)" = "Darwin" ] || { say "Jen pro macOS."; exit 1; }
if pgrep -x "Logic Pro X" >/dev/null || pgrep -x "Logic Pro" >/dev/null; then say "Zavri Logic (instalator by cekal)."; exit 1; fi
[ -d "$COMP" ] || { say "Plugin neni nainstalovany ($COMP) - nejdriv build.sh"; exit 1; }
OLD="$(ver "$COMP")"
say "Nainstalovano: $OLD"

WORK="$(mktemp -d)"
if [ $# -ge 1 ]; then
    ZIP="$1"
    ditto -x -k "$ZIP" "$WORK/x" || { say "zip nejde rozbalit"; exit 1; }
    NEWV="$(ver "$(find "$WORK/x" -maxdepth 3 -type d -name 'Rack Memory.component' ! -path '*/__MACOSX/*' | head -n 1)")"
    say "Balicek: $ZIP (verze $NEWV)"
else
    # testovaci nova verze: kopie nainstalovane s vyssim cislem, znovu podepsana (ad-hoc)
    NEWV="$(echo "$OLD" | awk -F. '{printf "%d.%d.%d", $1, $2, $3 + 1}')"
    mkdir -p "$WORK/s"
    ditto "$COMP" "$WORK/s/Rack Memory.component"
    "$PB" -c "Set :CFBundleShortVersionString $NEWV" "$WORK/s/Rack Memory.component/Contents/Info.plist"
    codesign --force --deep -s - "$WORK/s/Rack Memory.component" >/dev/null 2>&1
    ZIP="$WORK/RackMemory-$NEWV-test.zip"
    ditto -c -k --sequesterRsrc "$WORK/s" "$ZIP"
    say "Testovaci balicek $NEWV (kopie $OLD s jinym cislem): $ZIP"
fi

H0="$(libhash)"
say "Otisk knihovny pred: $H0"
say "1) instalace $OLD -> $NEWV"
T0=$(date +%s)
R="$(run_installer install "$ZIP" "$NEWV")"
say "   vysledek: $R ($(( $(date +%s) - T0 )) s)"
check "instalator hlasi installed" '[ "$R" = "installed" ]'
check "na disku je $NEWV" '[ "$(ver "$COMP")" = "$NEWV" ]'
check "zaloha $OLD existuje" '[ -d "$STATE/Backups/Rack Memory $OLD.component" ]'
check "auval PASS" 'auval -v aufx Rkmm Dusx 2>&1 | grep -q "AU VALIDATION SUCCEEDED"'
check "knihovna beze zmeny (verze, presety, rack, opravy, fotky)" '[ "$(libhash)" = "$H0" ]'

say "2) vraceni predchozi verze"
R="$(run_installer rollback - -)"
say "   vysledek: $R"
check "instalator hlasi rolledBack" '[ "$R" = "rolledBack" ]'
check "na disku je zase $OLD" '[ "$(ver "$COMP")" = "$OLD" ]'
check "auval PASS" 'auval -v aufx Rkmm Dusx 2>&1 | grep -q "AU VALIDATION SUCCEEDED"'
check "knihovna beze zmeny" '[ "$(libhash)" = "$H0" ]'

if [ $# -ge 1 ]; then
    say "3) znovu $NEWV (skutecny balicek zustane nainstalovany)"
    R="$(run_installer install "$ZIP" "$NEWV")"
    check "installed $NEWV" '[ "$R" = "installed" ] && [ "$(ver "$COMP")" = "$NEWV" ]'
fi
rm -f "$STATE/install-status.json"     # test nema plugin strasit hlaskou o aktualizaci
rm -rf "$WORK"
say ""
if [ $FAILS -eq 0 ]; then say "VYSLEDEK: VSE OK"; else say "VYSLEDEK: $FAILS CHYB - viz $STATE/install.log"; fi
say "Report: $REPORT"
exit $FAILS
