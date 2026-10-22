# Rack Memory 0.12.0: aktualizace, sada fotek, rozpoznávání

Zadání: „RACK MEMORY 0.12 — PHOTO DATASET, RECOGNITION & WORKING UPDATER“ (22. 10. 2026).

Testy proběhly v Linuxu (2 jádra). Mac, Logic ani skutečné vydání 0.12 na GitHubu jsem neměl k dispozici. Co z toho plyne, je v sekci 6.

---

## 1. Proč aktualizace nefungovala

1. **Nebylo co aktualizovat.**
   - Na GitHubu je jediné vydání: **v0.8.0** (13. 10.). Verze 0.9, 0.10 ani 0.11 nikdy vydané nebyly, protože `Tools/release.sh` se nespustil.
   - `latest.json` proto hlásí 0.8.0. Tvoje 0.11 i kamarádi s 0.8 dostanou „aktuální verze“ a žádné tlačítko se neobjeví.
   - Ověřeno přes GitHub API: na `latest.json` bylo 29 dotazů, `RackMemory-0.8.0-mac.zip` se stáhl jednou.
2. **Instalace přímo z běžícího Logicu.** Stará verze přesunula načtený plugin, zkopírovala nový a v běžícím Logicu zabila `AudioComponentRegistrar` (-9). Hrozí pád nebo zmatený seznam pluginů a úspěch se hlásil podle verze v Info.plist, ne podle toho, jestli plugin opravdu jde načíst.
3. **Chyběly kroky, které zadání chce:**
   - velikost před stažením,
   - zrušení stahování a zobrazení rychlosti,
   - stav „staženo – připraveno k instalaci“,
   - potvrzení instalace,
   - záloha a vrácení předchozí verze,
   - kontrola obsahu zipu (verze a procesor).

## 2. Nový postup (0.12)

| krok | co se děje | kde |
|---|---|---|
| 1. Zkontrolovat | `latest.json` z GitHub Releases → verze, **velikost**, poznámky; kontrola macOS a procesoru | plugin, na pozadí |
| 2. Stáhnout | po potvrzení; tlačítko ukazuje **% a rychlost**, klik = **zrušit** (nic se nezmění) | `~/Music/Rack Memory/Updates/staged` |
| 3. Ověřit | velikost + **SHA-256**. Obsah zipu se čte bez rozbalení: `Rack Memory.component`, ID `com.dusix.rackmemory`, verze v Info.plist = verze z GitHubu, **architektura binárky** (Mach-O hlavička) = tento Mac, minimální macOS | plugin |
| 4. Připraveno | **„INSTALOVAT x.y.z“** – stav přežije zavření Logicu (při dalším otevření se soubor znovu ověří) | plugin |
| 5. Instalace | jen po potvrzení. Plugin **nic nepřepisuje**: spustí samostatný instalátor (`rackmemory-install.sh`, odpojený proces) | instalátor |
| 6. Čekání | instalátor čeká, až binárku pluginu nikdo nemá načtenou (`lsof`) a neběží Logic / MainStage; jde zrušit | instalátor |
| 7. Záloha | současná verze do `Updates/Backups` (poslední 2) | instalátor |
| 8. Výměna | nová verze se rozbalí vedle (`ditto`), znovu se zkontroluje verze a procesor (`file`), pak se přejmenuje na místo staré | instalátor |
| 9. Ověření | `AudioComponentRegistrar` se obnoví a **`auval`** musí hlásit „AU VALIDATION SUCCEEDED“; jinak se vrátí původní verze | instalátor |
| 10. Výsledek | `install-status.json` (installed / failed / rolledBack / cancelled). Plugin ho ukáže při dalším otevření; běží-li stará verze, ukáže **RESTARTUJ LOGIC** | plugin |

Další funkce:
- ••• → **Vrátit předchozí verzi pluginu** (také až po zavření Logicu).
- Zip ze Stažených nebo „Aktualizovat ze souboru…“ projde stejnou kontrolou i instalací.
- Kdyby se instalátor nespustil sám, v `Updates` je **`Instalovat aktualizaci Rack Memory.command`** pro dvojklik.
- Instalátor nepoužívá `lipo`. Bez Command Line Tools by macOS ukázal dialog, proto se architektura ověřuje přes `/usr/bin/file`.

**Kamarádi s 0.8 až 0.11** mají ve svém pluginu ještě starý updater, ten změnit nejde. `release.sh` proto zapisuje `minimum_version: 0.12.0`. Starší verze tak ukážou „NOVÁ VERZE“ a otevřou stránku vydání (ruční instalace) a nebudou riskovat přepsání načteného pluginu. Od 0.12 výš už to jde automaticky.

## 3. Co je ověřené

| | výsledek |
|---|---|
| Skutečné vydání v0.8.0 z GitHubu (stažené přes curl) | `latest.json` platný; velikost 9 062 790 B **sedí**; SHA-256 **sedí**; obsah OK (verze 0.8.0, `com.dusix.rackmemory`, x86_64 + arm64) |
| Instalační skript na tomto skutečném zipu | záloha 0.11.0 → instalace 0.8.0 → `installed`. Spuštěno v Linuxu: macOS nástroje (ditto, PlistBuddy, file, lsof, auval) nahradily jednoduché náhrady |
| Automatické testy updateru | špatný součet, špatná velikost, jiná verze v zipu, jen Apple Silicon, cizí plugin, poškozený zip, offline, cizí doména, `minimum_version`, `macos_min`, **zrušení při 10 %**, stav po restartu, výsledky instalátoru (failed / waiting po restartu Macu / installed) |
| Instalační skript (testy) | úspěch se zálohou; **auval selže → vrácena původní**; špatná verze → nic se nezmění; **Logic běží → čeká**; zrušení během čekání; po „zavření Logicu“ doběhne; **vrácení předchozí verze** |

**Neověřené:**
- Spuštění instalátoru z pluginu uvnitř skutečného Logicu. Pokud by ho Logic nebo macOS zablokoval, plugin to ohlásí a nabídne `.command`.
- Skutečné `auval`, `lsof` a `ditto` na Macu.
- Stažení přes síťovou vrstvu JUCE na Macu. Linuxový build ji nemá, stahoval jsem přes curl.
- Celý postup se skutečným vydáním 0.12.0, které zatím neexistuje.

## 4. Fotky

### Kolik

| | počet |
|---|---|
| fotek hardwaru ve tvých Stažených (prošel jsem 43 souborů; osobní fotky, které s projektem nesouvisí, jsem hned smazal) | 20 použitelných + 4 duplicitní |
| nalezeno na webu (inzeráty, obchody, manuál) | 10 kandidátů v `catalog.json → candidates`, **nestaženo** |
| volně licencované (Creative Commons) fotky těchto přístrojů | **0** – žádné jsem nenašel |
| legálně použitelné **pro lokální test** | 20: 1 tvoje fotka racku + 19 produktových fotek výrobců a obchodů. Nejsou v gitu a nešíří se |
| z toho **reference** (podle nich je nakreslený panel, test by byl zkreslený) | 4 (xfilter ME, xfilter 1U, Vitalizer, RHEA) |
| testovacích (originály) | 16, z toho 3 negativní (nepodporovaný model – nesmí se nic najít) |
| ztížené varianty (tma, rozmazání, ½ a ¼ rozlišení, šikmý pohled, JPEG 30 %) | 42 (z 7 fotek s pravdou) |
| s **ověřenými** hodnotami (kontrola na skutečném přístroji) | **0** |
| s hodnotami **odhadnutými okem**, jen jasně čitelné ovladače | 7 fotek, 96 ovladačů (rack 53, Vitalizer 8, xfilter ME 14, xmax 3 tlačítka, 3× RHEA 6 „nesmí se přečíst“) |

Poznámky ke zdrojům:
- Z tohoto prostředí jsem se na weby výrobců ani na Wikimedii nedostal (blokuje to síťová politika). Obcházet jsem to nezkoušel.
- Fotky z inzerátů (Reverb, finn.no) jsou nejcennější typ fotek, ale potřebují svolení. Vedu je jako kandidáty.
- Screenshoty obchodů (WA-2A, WA76, Samson, Neutrik) jsou vyřazené: přístroj na nich má asi 100 px.
- Knoby na renderech elysia 500 nejsou jako pravda dost čitelné, takže jsou bez pravdy. Měří se na nich jen nalezení přístroje.

### Výsledky podle modelu (sada 58 snímků, 0.12)

| model | snímků | nalezeno | ovladače správně / s pravdou | špatně | nejisté |
|---|---|---|---|---|---|
| SPL Vitalizer Mk3-T | 14 | 14/14 | 93/112 | 0 | 19 |
| WesAudio _RHEA | 30 | 28/30 | 174/182 | 0 | 8 |
| elysia xmax 500 | 14 | 11/14 | 35/119 | 0 | 84 |
| elysia xfilter ME | 8 | 7/8 | 28/98 | 3 | 67 |
| elysia nvelope 500 | 8 | 6/8 | 36/105 | 3 | 66 |
| elysia xfilter 1U | 7 | 4/7 | 8/56 | 2 | 46 |
| elysia xfilter 500 | 2 | 2/2 | – | – | – |
| negativní (nvelope 19", xmax 19", Manley) | 3 | 0 falešných nálezů | | | |

### Podle typu ovladače

| typ | správně / s pravdou | špatně |
|---|---|---|
| knob s ryskou (SPL) | 93/112 | 0 |
| LED kruh (RHEA, má zůstat nezadané) | 168/168 | 0 |
| LED / tlačítko | 68/147 | 0 |
| knob se zářezem (elysia) | 15/56 | 3 |
| stupňový knob (elysia 41 kroků) | 30/189 | 5 |

### Podle podmínek

| podmínky | nalezeno | správně | špatně |
|---|---|---|---|
| originál | 15/17 | 77/96 | 2 |
| tma | 11/11 | 46/96 | 0 |
| rozmazaná | 8/11 | 46/96 | 0 |
| poloviční rozlišení | 11/11 | 63/96 | 3 |
| čtvrtinové rozlišení | 8/11 | 39/96 | 0 |
| šikmo | 8/11 | 33/96 | 0 |
| JPEG 30 % | 11/11 | 70/96 | 3 |

### 0.11 → 0.12 na stejné sadě

| | 0.11 | **0.12** |
|---|---|---|
| nalezeno přístrojů | 60/83 | **72/83** |
| falešné nálezy | 3 | **0** |
| ovladače správně | 348/672 | **374/672** |
| ovladače špatně | 14 | **8** |
| pevná syntetická sada (70 snímků) | 761/910, 1 špatně | 761/910, 1 špatně (beze změny) |

Co jsem změnil v rozpoznávání:
- **Tmavá nebo malá fotka:** „LED nesvítí“ se tvrdí jen tam, kde by svítící LED byla vidět (expozice ≥ 0,45 a LED aspoň 1,5 px). Dřív se tmavá fotka xmaxu přečetla jako tři vypnutá tlačítka a malá fotka xfilteru jako šest vypnutých.
- **Menší fotka celého racku (pod 1900 px):** při neúspěchu druhý průchod se zvětšením 1,8×. Poloviční rozlišení: 2/5 → 5/5 přístrojů, 15 → 36 ovladačů správně.
- **Více hypotéz:** každý průchod vyhledávání dá vlastní hypotézu a rozhodne shoda potisku. Opravuje to Vitalizer v polovičním rozlišení, který zvětšený průchod jinak přebil.

## 5. Stabilita

- CPU a paměť při analýze celého racku: 13,8 s, 1 jádro, 0,51 GB, stejně jako 0.11. U menší fotky se přidá jen zvětšovací průchod: 7,9 s, 0,40 GB.
- Stahování aktualizace běží v jednom vlákně na pozadí a dá se zrušit. Instalátor běží mimo Logic a po skončení zmizí.
- Ruční a potvrzené hodnoty fotka dál nepřepisuje (beze změny od 0.11). Projekty, presety, knihovna hardwaru i formát dat zůstávají kompatibilní.

## 6. Testy, buildy, co zbývá

- **Linux:**
  - 2269 kontrol OK, 0 selhalo;
  - clang-18 bez nových varování;
  - pojistky v testech: pevná syntetická sada a sada fotek (lokálně) se nesmí zhoršit.
- **Mac:** nesestaveno a v Logicu neotestováno.
- **Nevydáno:** 0.12 na GitHubu není. Vydat ji musíš z Macu (`bash RackMemory/Tools/release.sh "poznámky"`), protože binárku pro Mac odsud postavit neumím.
- **Zbývá:**
  - zářezy na stříbrných knobech xfilteru (3 + 2 špatně na fotkách);
  - nvelope Freq A na fotce racku, kde se čtení liší od mého odhadu okem o 25°;
  - skutečné fotky s ověřenými hodnotami (fotka + současně zapsané hodnoty z přístroje).

## 7. Postup pro tebe

1. Build:

       cd ~/Downloads && rm -rf RackMemory && unzip -oq RackMemory-0.12.0.zip && bash RackMemory/build.sh

2. Vydání (z Macu, nejlépe s přihlášeným `gh`):

       bash RackMemory/Tools/release.sh "0.12: bezpecne aktualizace, lepsi cteni z mensich a tmavych fotek"

   Pak ověř: `bash RackMemory/Tools/release.sh --verify`.

3. Test updateru na Macu: potřebuje dvě vydání. Plugin nabízí jen vyšší verzi a balíček musí mít verzi, kterou hlásí GitHub.
   1. Vydej 0.12.0 a nainstaluj ji.
   2. V `CMakeLists.txt` zvyš verzi na 0.12.1 a vydej znovu.
   3. V Logicu s 0.12.0 projdi: AKTUALIZACE → Stáhnout (sleduj procenta, zkus Zrušit) → INSTALOVAT → potvrdit → zavři Logic → otevři Logic. Plugin musí ukázat „Aktualizováno na 0.12.1“.
   4. Kontrola: `~/Music/Rack Memory/Updates/install.log` a `Backups/`.
   5. Zkus i ••• → Vrátit předchozí verzi.
4. Sada fotek u tebe: `python3 RackMemory/Tools/photo_dataset_from_downloads.py` (zkopíruje fotky ze Stažených a vytvoří varianty).
