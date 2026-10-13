# Rack Memory 0.8.0

AU plugin pro Logic Pro: paměť nastavení analogového racku + hlasitost a měření do/z hardwaru. Celý rack je na jedné stránce. Hardware se **neovládá**; zvuk prochází beze změny (jen volitelný gain, 0 dB = bitově beze změny).

Rack: SPL Vitalizer Mk3-T, elysia xfilter Mastering Edition (1U), a v lunchboxu elysia xmax 500, nvelope 500 a WesAudio _RHEA.

## Instalace (u tebe)

Dvojklik na `Instalovat Rack Memory.command` (ve složce RackMemory), nebo v Terminálu:

    cd ~/Downloads && unzip -oq RackMemory-0.8.0.zip && bash RackMemory/build.sh

Build je univerzální (Intel i Apple Silicon) a na konci uloží do Stažených **`Rack Memory 0.8.0.zip`**, hotový balíček pro kamarády.

## Pro kamaráda (bez Terminálu)

Pošli mu jen `Rack Memory <verze>.zip`. Uvnitř je plugin a návod `JAK NAINSTALOVAT.txt`:
první instalace = přetáhnout `Rack Memory.component` do `~/Library/Audio/Plug-Ins/Components`.
**Další verze se instalují samy:** plugin se podívá na GitHub (github.com/DusiXxx/MEMORY-RACK), a když je novější verze,
ukáže zelené tlačítko **AKTUALIZOVAT x.y.z**. Jedno kliknutí → stáhne, ověří kontrolní součet, nainstaluje → **RESTARTUJ LOGIC**.
Ručně: ••• → Zkontrolovat aktualizace. Bez internetu: nový zip do Stažených, ••• → Aktualizovat ze souboru…
Vydání nové verze: viz `RELEASING.md` (`bash Tools/release.sh "co je nového"`).

## Nastavení z fotky

1. Vyfoť přístroj zepředu, celý, ostře (bez blesku do odlesku).
2. Fotku pusť **kamkoli do pluginu**: plugin sám pozná, který přístroj na ní je (víc přístrojů na jedné fotce = víc najednou). Nebo ji pusť přímo na přístroj / jeho políčko dole.
3. Ovladače se nastaví. **Modrá tečka** = přečteno z fotky, **oranžová** = nejisté, nenastaveno (nehádá se). **Zpět** vrátí stav. Fotka je dole v políčku přístroje (klik na náhled = zvětšit); vzhled panelů se podle fotky nemění.

Přístroj se na fotce hledá podle potisku (písmena, stupnice, šrouby), takže nevadí pozadí, vodoznak, okolní věci v racku ani mírně šikmý pohled.
Čte se: elysia (zářez na knobu, LED, svítící tlačítka), SPL (ryska na knobu, ON). _RHEA zatím ne, fotka se jen uloží.

## Hlasitost do a z hardwaru

Pruh pod horní lištou: měření (LUFS podle BS.1770, ryska = špička) a **gain** (−24 … +24 dB, dvojklik = 0 dB).
- **Jedna instance** (např. na začátku chainu): „Úroveň“ = měřič + gain.
- **Dvě instance** kolem Logic I/O pluginu s hardwarem: první přepni na **IN – před HW** (gain = jak horký signál jde do hardwaru), druhou na **OUT – za HW** (gain = vyrovnání návratu). Obě ukazují IN i OUT, **HW: ±x dB** = co přidává/ubírá samotný hardware, **Srovnat** nastaví OUT tak, aby byl stejně hlasitý jako IN (férové A/B s hardwarem a bez). Víc chainů v projektu = „řetěz 1–4“.
- Gain IN i OUT se ukládá k verzi skladby a při načtení verze se vrátí.

## Psaní čísel

- **Dvojklik na knob** (nebo zapni **123** a klikni) → napiš hodnotu z potisku: `2.5`, `-0.3`, `1k5`, `16k`, `OFF`, `MAX` … Enter.
- **Tab** = další knob (i na další přístroj), Shift+Tab zpět. `?` = nezadáno.
- Mezi čísly potisku jde napsat i mezihodnotu (uloží se nejbližší krok). U elysia 500 se píše krok `1–41`, u knobů bez čísel `2:30` (hodiny) nebo `0–10`.

## Můj rack (jednou sestavit, platí pro všechny projekty)

- **+ přístroj** (poslední políčko dole): přidat z knihovny, klidně dva stejné kusy (druhý je „xmax 500 (2)“ a má vlastní hodnoty).
- **×** na políčku přístroje (při najetí myší): odebrat z racku. Uložená nastavení ve verzích zůstanou, přístroj jde přidat zpět.
- **Přesun:** 19" přístroj chyť mimo knoby a přetáhni mezi řádky. 500 modul přetáhni na jiný slot lunchboxu (přichytí se, ostatní uhnou; červeně = není místo). Celý lunchbox chyť za šasi.
- Pravé tlačítko na lunchbox: počet slotů (6/8/10/11/16).
- Rack se ukládá hned do `~/Music/Rack Memory/rack.json` (+ záloha `rack.json.bak`) a každý další projekt ho převezme. Verze skladby obsahují jen hodnoty knobů.
- Když verze obsahuje hodnoty přístroje, který v racku není, ukáže to políčko „+“ a dá ho jedním klikem přidat.
- ••• → Vrátit výchozí rack.

## Ostatní

- **ULOŽIT**: jedno kliknutí, vždy nová verze skladby (v1, v2, …), nic se nepřepisuje. **Načíst**: seznam nahoře.
- Knob: táhni nebo kolečko myši. Tlačítko: klik. Pravé tlačítko = přesná poloha.
- Data: `~/Music/Rack Memory/` (`Snapshots/`, `Photos/`, `Trash/`, `Reports/`, `rack.json`).

## Panely

SPL, WesAudio: oficiální čelní fotografie výrobce. elysia 500: složeno z oficiálních recall sheetů. xfilter Mastering Edition: čelní fotografie 1U xfilteru elysia s potiskem gainu podle Mastering Edition (±5 dB). Knoby elysia jsou vykreslené. Obrázky jsou pro osobní použití, plugin s nimi veřejně nešiř.

## Ověření

Offline testy (`RM_BUILD_TESTS=ON`): 1514 kontrol, mimo jiné 20 syntetických scén se známou pravdou (238/272 přečteno, 0 špatně) a fotky výrobců/obchodů (Vitalizer z Thomannu 9/9 správně). Na skutečných fotkách z telefonu ověřené není.
