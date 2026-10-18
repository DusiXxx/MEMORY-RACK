# Jak vydat novou verzi MEMORY RACK (automatická aktualizace)

Plugin se při otevření okna (nejvýš 1× za 6 h, nebo ••• → Zkontrolovat aktualizace) podívá na
`https://github.com/DusiXxx/MEMORY-RACK/releases/latest/download/latest.json`.
Když je tam vyšší verze, ukáže zelené tlačítko **AKTUALIZOVAT x.y.z**. Klik → stáhne zip, ověří
velikost a SHA-256, nahradí `~/Library/Audio/Plug-Ins/Components/Rack Memory.component` (stará verze
se při chybě vrátí) → **RESTARTUJ LOGIC**. Bez hesla, bez Terminálu. Nastavení a verze skladeb se nemění.

## Jednou na začátku
1. Na https://github.com/new vytvoř repozitář **MEMORY-RACK**, **Public** (jinak by kamarádi nemohli stahovat).
2. (volitelně) `brew install gh && gh auth login` – pak se vydává jedním příkazem.

## Každé vydání
1. V `CMakeLists.txt` zvyš verzi: `project(RackMemory VERSION 0.8.1 ...)` (musí být vyšší než minulá).
2. V Terminálu:

       cd ~/Downloads/RackMemory && bash Tools/release.sh "Co je nového"

   Skript postaví plugin (Intel + Apple Silicon, auval), udělá `RackMemory-<verze>-mac.zip`
   a `latest.json` ve složce `~/Downloads/MemoryRack-release-<verze>`.
3. S `gh`: potvrdíš `a` a je hotovo. Bez `gh`: otevře se stránka nového vydání – Tag `v<verze>`,
   přetáhni oba soubory (zip + latest.json), **Publish release** (ne pre-release).
4. Kontrola: `bash Tools/release.sh --verify` → „OK: GitHub nabízí verzi …“.

## Pravidla
- Nikdy neměň soubory už vydané verze – vždy nová verze s vyšším číslem.
- `latest.json` musí být u vydání označeného jako *Latest*.
- Adresa repozitáře je zapečená v pluginu (`RM_UPDATE_REPO` v CMakeLists.txt). Změna repozitáře = nová verze
  ručně všem, kdo mají starou.
- Plugin nic nespouští, jen nahradí `.component` – a jen když sedí kontrolní součet.

## Knihovna hardwaru (zvlášť od pluginu)
Nové / opravené profily přístrojů jdou vydat bez nové verze pluginu:

    bash Tools/release_library.sh 2 "WA-2A: opravené rozmístění"

Složka `Tools/hardware-library/` obsahuje `library.json` (`{"schema":1,"libraryVersion":2,"profiles":[…]}`) a `images/`.
Vydání má tag **hardware** a **nesmí být označené jako Latest** (jinak by se plugin nedostal k `latest.json` pluginu).
Pravidla: ID přístrojů a ovladačů se nemění; upravený profil = vyšší `profileVersion` a všechna původní ID ovladačů.
