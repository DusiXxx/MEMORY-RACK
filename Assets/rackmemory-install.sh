#!/bin/sh
# Rack Memory - samostatny instalator aktualizace (bezi MIMO Logic, plugin ho jen spusti).
#
#   rackmemory-install.sh install  <overeny.zip> <ocekavana verze> <slozka Components> <slozka Updates>
#   rackmemory-install.sh rollback -               -                <slozka Components> <slozka Updates>
#
# 1. ceka, az plugin nepouziva zadny program (Logic zavreny; nactena knihovna pluginu nesmi byt prepsana)
# 2. zalohuje soucasnou verzi do <Updates>/Backups
# 3. rozbali novou verzi vedle, zkontroluje verzi a architekturu, pak teprve vymeni
# 4. oznami macOS novou verzi (AudioComponentRegistrar) a overi ji (auval)
# 5. pri jakekoli chybe vrati zalohu
# Vysledek zapisuje do <Updates>/install-status.json (plugin ho precte pri dalsim otevreni).
# Zruseni behem cekani: soubor <Updates>/cancel.
#
# Prikazy lze pro testy nahradit promennymi RM_* (na macOS se pouziji systemove).

MODE="$1"; ZIP="$2"; VER="$3"; COMPDIR="$4"; STATE="$5"
DITTO="${RM_DITTO:-/usr/bin/ditto}"
PLISTBUDDY="${RM_PLISTBUDDY:-/usr/libexec/PlistBuddy}"
FILECMD="${RM_FILE:-/usr/bin/file}"          # ne lipo: to je jen s Command Line Tools (jinak macOS ukaze dialog)
LSOF="${RM_LSOF:-/usr/sbin/lsof}"
PGREP="${RM_PGREP:-/usr/bin/pgrep}"
AUVAL="${RM_AUVAL:-/usr/bin/auval}"
KILLALL="${RM_KILLALL:-/usr/bin/killall}"
XATTR="${RM_XATTR:-/usr/bin/xattr}"
ARCH="${RM_ARCH:-$(uname -m)}"
WAIT_STEP="${RM_WAIT_STEP:-3}"
MAX_WAIT="${RM_MAX_WAIT:-1209600}"        # 14 dni; pak se to vzda (plugin to nabidne znovu)

NAME="Rack Memory.component"
COMP="$COMPDIR/$NAME"
BACKUPS="$STATE/Backups"
STATUS="$STATE/install-status.json"
mkdir -p "$STATE" "$BACKUPS"

log () { echo "$(date '+%Y-%m-%d %H:%M:%S') $*"; }
status () {   # stav verze predchozi zprava
    printf '{"state":"%s","version":"%s","previous":"%s","message":"%s","time":%s,"pid":%s,"mode":"%s"}\n' \
        "$1" "$2" "$3" "$4" "$(date +%s)" "$$" "$MODE" > "$STATUS.tmp" && mv -f "$STATUS.tmp" "$STATUS"
    log "stav: $1 $2 ($4)"
}
plist_version () { "$PLISTBUDDY" -c "Print :CFBundleShortVersionString" "$1/Contents/Info.plist" 2>/dev/null; }

busy () {
    # nekdo ma nactenou knihovnu pluginu (Logic, AUHostingService, jiny hostitel), nebo bezi Logic / MainStage
    if [ -e "$COMP/Contents/MacOS/Rack Memory" ] && [ -n "$("$LSOF" -t "$COMP/Contents/MacOS/Rack Memory" 2>/dev/null)" ]; then return 0; fi
    for p in "Logic Pro X" "Logic Pro" "MainStage" "MainStage 3"; do
        "$PGREP" -x "$p" >/dev/null 2>&1 && return 0
    done
    return 1
}

validate () {   # 0 = auval potvrdil, ze plugin jde nacist
    [ -x "$AUVAL" ] || { log "auval neni k dispozici - overeni preskoceno"; return 0; }
    "$KILLALL" -9 AudioComponentRegistrar >/dev/null 2>&1
    sleep 1
    "$AUVAL" -v aufx Rkmm Dusx 2>&1 | grep -q "AU VALIDATION SUCCEEDED"
}

OLD="$(plist_version "$COMP")"
[ -z "$OLD" ] && OLD="-"
rm -f "$STATE/cancel"

if [ "$MODE" = "install" ]; then
    [ -f "$ZIP" ] || { status failed "$VER" "$OLD" "balicek chybi"; exit 1; }
elif [ "$MODE" = "rollback" ]; then
    LAST="$(cat "$BACKUPS/last" 2>/dev/null)"
    [ -n "$LAST" ] && [ -d "$LAST" ] || { status failed "-" "$OLD" "zadna zaloha"; exit 1; }
    VER="$(plist_version "$LAST")"
else
    echo "pouziti: $0 install|rollback ..."; exit 2
fi

# ---- 1. cekani, az plugin nikdo nepouziva
WAITED=0
while busy; do
    [ -f "$STATE/cancel" ] && { rm -f "$STATE/cancel"; status cancelled "$VER" "$OLD" "zruseno uzivatelem"; exit 0; }
    [ $WAITED -ge "$MAX_WAIT" ] && { status failed "$VER" "$OLD" "Logic se nezavrel - instalace odlozena"; exit 1; }
    [ $WAITED -eq 0 ] && status waiting "$VER" "$OLD" "ceka na zavreni Logicu"
    sleep "$WAIT_STEP"; WAITED=$((WAITED + WAIT_STEP))
done
[ -f "$STATE/cancel" ] && { rm -f "$STATE/cancel"; status cancelled "$VER" "$OLD" "zruseno uzivatelem"; exit 0; }
status installing "$VER" "$OLD" "instaluji"

# ---- 2. priprava nove verze (vedle, nic se zatim nemeni)
WORK="$STATE/work.$$"
rm -rf "$WORK"; mkdir -p "$WORK"
if [ "$MODE" = "install" ]; then
    "$DITTO" -x -k "$ZIP" "$WORK/unz" || { rm -rf "$WORK"; status failed "$VER" "$OLD" "balicek nejde rozbalit"; exit 1; }
    NEW="$(find "$WORK/unz" -maxdepth 3 -type d -name "$NAME" ! -path "*/__MACOSX/*" | head -n 1)"
else
    NEW="$LAST"
fi
[ -n "$NEW" ] && [ -f "$NEW/Contents/Info.plist" ] || { rm -rf "$WORK"; status failed "$VER" "$OLD" "v balicku neni plugin"; exit 1; }
GOT="$(plist_version "$NEW")"
[ "$GOT" = "$VER" ] || { rm -rf "$WORK"; status failed "$VER" "$OLD" "balicek ma verzi $GOT, ocekavana $VER"; exit 1; }
"$FILECMD" -b "$NEW/Contents/MacOS/Rack Memory" 2>/dev/null | grep -qw "$ARCH" \
    || { rm -rf "$WORK"; status failed "$VER" "$OLD" "plugin neni pro tento procesor ($ARCH)"; exit 1; }
"$DITTO" "$NEW" "$WORK/$NAME" || { rm -rf "$WORK"; status failed "$VER" "$OLD" "kopie selhala (plny disk?)"; exit 1; }
"$XATTR" -dr com.apple.quarantine "$WORK/$NAME" >/dev/null 2>&1

# ---- 3. zaloha soucasne verze (rollback zalohu nevytvari znovu)
BACKUP=""
if [ -d "$COMP" ] && [ "$MODE" = "install" ]; then
    BACKUP="$BACKUPS/Rack Memory $OLD.component"
    rm -rf "$BACKUP"
    "$DITTO" "$COMP" "$BACKUP" || { rm -rf "$WORK" "$BACKUP"; status failed "$VER" "$OLD" "zaloha selhala - nic se nezmenilo"; exit 1; }
    printf '%s' "$BACKUP" > "$BACKUPS/last"
    # nechat jen 2 posledni zalohy
    ls -1td "$BACKUPS"/*.component 2>/dev/null | tail -n +3 | while IFS= read -r d; do [ "$d" = "$BACKUP" ] || rm -rf "$d"; done
fi

# ---- 4. vymena (prejmenovani v ramci jedne slozky)
mkdir -p "$COMPDIR"
rm -rf "$COMPDIR/.rm-old.component"
[ -d "$COMP" ] && mv "$COMP" "$COMPDIR/.rm-old.component"
if ! mv "$WORK/$NAME" "$COMP"; then
    [ -d "$COMPDIR/.rm-old.component" ] && mv "$COMPDIR/.rm-old.component" "$COMP"
    rm -rf "$WORK"; status failed "$VER" "$OLD" "vymena selhala - vracena puvodni"; exit 1
fi
rm -rf "$WORK"

# ---- 5. overeni; pri chybe navrat
NOWV="$(plist_version "$COMP")"
if [ "$NOWV" = "$VER" ] && validate; then
    rm -rf "$COMPDIR/.rm-old.component"
    if [ "$MODE" = "rollback" ]; then status rolledBack "$VER" "$OLD" "vracena predchozi verze"
    else status installed "$VER" "$OLD" "nainstalovano a overeno (auval)"; fi
    exit 0
fi
log "overeni selhalo (na disku $NOWV) - vracim puvodni"
rm -rf "$COMP"
if [ -d "$COMPDIR/.rm-old.component" ]; then
    mv "$COMPDIR/.rm-old.component" "$COMP"
    "$KILLALL" -9 AudioComponentRegistrar >/dev/null 2>&1
fi
status failed "$VER" "$OLD" "nova verze neprosla overenim (auval) - vracena $OLD"
exit 1
