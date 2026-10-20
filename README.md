# Rack Memory 0.11.0

AU plugin pro Logic Pro: paměť nastavení analogového racku + hlasitost a měření do/z hardwaru. Celý rack je na jedné stránce. Hardware se **neovládá**; zvuk prochází beze změny (jen volitelný gain, 0 dB = bitově beze změny).

Skupiny (záložky): **Mixing**, **Mastering** a vlastní – každá má svůj rack a své verze. Nahoře **PATCHBAY** (Samson S-patch Plus, Neutrik NYS-SPP-L1).
Přístroje: SPL Vitalizer Mk3-T, elysia xfilter (1U i Mastering Edition), xmax 500, nvelope 500, WesAudio _RHEA, Warm Audio WA76, WA-2A, API 512c, AMS Neve 1073LB, Cranborne Camden 500 + vlastní přístroje (+ PŘÍSTROJ → Vlastní přístroj).

**Co je nového: `docs/STABILITA-0.11.md` (0.11 – zahřívání, regrese čtení z fotky, pevná regresní sada), `docs/FOTO-A-KNIHOVNA-0.10.md` (0.10) a `docs/OPRAVY-0.9.md` (0.9) – včetně postupu testu v Logicu.**

## Instalace (u tebe)

Dvojklik na `Instalovat Rack Memory.command` (ve složce RackMemory), nebo v Terminálu:

    cd ~/Downloads && rm -rf RackMemory && unzip -oq RackMemory-0.11.0.zip && bash RackMemory/build.sh

Build šetří počítač: jen pro tento Mac, o jedno jádro méně než má procesor (`RM_JOBS=2 bash RackMemory/build.sh` = ještě méně),
s nízkou prioritou, a složka buildu je v `~/Library/Caches/RackMemoryBuild` – při další verzi se překládá jen to, co se změnilo.
Na konci uloží do Stažených `Rack Memory 0.11.0 (x86_64).zip` (jen pro Mac se stejným procesorem).
Univerzální plugin (Intel i Apple Silicon) pro kamarády: `bash RackMemory/build.sh --universal` (dělá ho i `Tools/release.sh`).

## Pro kamaráda (bez Terminálu)

Pošli mu jen `Rack Memory <verze>.zip`. Uvnitř je plugin a návod `JAK NAINSTALOVAT.txt`:
první instalace = přetáhnout `Rack Memory.component` do `~/Library/Audio/Plug-Ins/Components`.
**Další verze se instalují samy:** plugin se podívá na GitHub (github.com/DusiXxx/MEMORY-RACK), a když je novější verze,
ukáže zelené tlačítko **AKTUALIZOVAT x.y.z**. Jedno kliknutí → stáhne, ověří kontrolní součet, nainstaluje → **RESTARTUJ LOGIC**.
Ručně: ••• → Zkontrolovat aktualizace. Bez internetu: nový zip do Stažených, ••• → Aktualizovat ze souboru…
Vydání nové verze: viz `RELEASING.md` (`bash Tools/release.sh "co je nového"`).

## Nastavení z fotky

1. Vyfoť přístroj zepředu, celý, ostře (bez blesku do odlesku).
2. Klikni **FOTO** nahoře, nebo fotku pusť **kamkoli do pluginu**: plugin sám pozná, který přístroj na ní je (víc přístrojů na jedné fotce = víc najednou). Nebo ji pusť přímo na přístroj / jeho políčko dole.
3. Ovladače se nastaví. **Modrá tečka** = přečteno z fotky, čeká na kontrolu (kroužek = potvrzeno); nejisté ovladače se NEZMĚNÍ a jsou v okně **Kontrola po fotce**. **Oranžová** = nezadáno.
   Nenašel-li plugin přístroj: pravé tlačítko na přístroj → **Zarovnat fotku ručně** (4 rohy). **Zpět** vrátí stav. Fotka je dole v políčku přístroje (klik na náhled = zvětšit); vzhled panelů se podle fotky nemění.

Ruční nebo potvrzené hodnoty fotka **nikdy sama nepřepíše**: když čte jinak, ukáže to v okně Kontrola po fotce a hodnoty převezmeš tlačítkem **Převzít z fotky** (jde vrátit Zpět).
Analýza běží v jednom vlákně s nízkou prioritou, stejná fotka podruhé = výsledek z paměti, nová fotka starou analýzu zruší, zavření okna analýzu zastaví.
Každá analýza zapíše řádek do `~/Music/Rack Memory/Reports/photo-analysis.log` (rozměr fotky, nalezené přístroje, časy) – pošli ho, když fotka dopadne jinak, než čekáš.
Potvrzené / opravené hodnoty po fotce se ukládají jako ověřené příklady do `~/Music/Rack Memory/Corrections/corrections.jsonl` (jen u tebe, nic se samo neučí).

Přístroj se na fotce hledá podle potisku (písmena, stupnice, šrouby), takže nevadí pozadí, vodoznak, okolní věci v racku ani mírně šikmý pohled.
Čte se: elysia (zářez na knobu, LED, svítící tlačítka), SPL (ryska na knobu, ON), _RHEA (LED kroužky a LED přepínačů – ověřeno jen na syntetických fotkách). WA76 / WA-2A zatím ne.

**Klik na knob** = zvětšený detail (potisk, hodnota, stupnice, stav, výřez tvojí fotky). ••• → Výrazné ukazatele = silnější ručičky.

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

Offline testy (`RM_BUILD_TESTS=ON`): přes 1700 kontrol, mimo jiné 20 syntetických scén se známou pravdou (238/272 přečteno, 0 špatně) a fotky výrobců/obchodů (Vitalizer z Thomannu 9/9 správně). Na skutečných fotkách z telefonu ověřené není.
