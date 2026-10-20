# Rack Memory 0.11.0: stabilita, výkon a regrese čtení z fotky

Zadání: „MEMORY RACK — CRITICAL STABILITY, CPU OPTIMIZATION & PHOTO RECOGNITION REGRESSION“ (20. 10. 2026).
Všechna čísla níže jsou naměřená. Kde chybí měření z Macu nebo z Logicu, je to tak napsané.

Testovací stroj: Linux, 2 jádra. Plugin v Logicu jsem nespouštěl, protože tady Mac není.

---

## 1. Příčina zahřívání

Příčinu jsem hledal ve zdrojácích a měřil ji na stejné fotce. Na Macu jsem měřit nemohl. Nekonečnou smyčku ani vlákno, které by běželo pořád, jsem nenašel.

### Kompilace (velmi pravděpodobně hlavní zdroj horka při buildu)

Podle tvého výpisu z Terminálu dělal `build.sh` toto:
- **Přeložil všechno dvakrat.** Build byl univerzální: `arm64;x86_64`, každý soubor pro obě architektury.
- **Použil všechna logická jádra** (`-j $(sysctl -n hw.ncpu)`).
- **Pokaždé začínal od nuly.** Příkaz `rm -rf RackMemory` smazal i složku `build`, takže se znovu překládal celý JUCE: asi 63 překladových jednotek pro každou architekturu, plus 67 s konfigurace.

Plné vytížení všech jader na několik minut stačí, aby se MacBook 2017 rozpálil.

### Plugin za běhu (horko a zátěž při analýze fotky)

| Kde | Co dělala 0.10 | Dopad |
|---|---|---|
| FOTO (celý rack) | jedno vlákno na každý přístroj, všechna najednou | při 4–5 přístrojích plná jádra na 20–40 s |
| totéž | každé vlákno si dělalo vlastní pracovní kopii fotky (3400 px, 4 barevné roviny) a souběžně počítalo stejné výrazné body | špička paměti **1,77 GB** (měřeno), může vést k odkládání na disk |
| další FOTO / jiná fotka | starší analýza se nerušila | víc analýz najednou |
| zavření okna | analýza běžela dál až do konce | CPU běží i po zavření pluginu |
| otevření projektu | uložená fotka celého racku se dekódovala jednou za každý přístroj, souběžně | 4–5× dekódování 12 Mpx fotky |
| časovač okna | každé 2 s procházel složku Snapshots, každých 10 s Stažené a Plochu | malé, ale trvalé dotazování |
| měřič hlasitosti | překresloval 25× za sekundu i v tichu | malé, ale trvalé |
| audio vlákno | jen gain a měřič (K-vážení) | v pořádku, nic se nemění |

## 2. Kompilace, nebo běh?

**Obojí.** Kompilace vytížila všechna jádra na několik minut. V pluginu běžel souběžný výpočet na všech jádrech a s velkou spotřebou paměti, a to při každé fotce.

Klidový stav pluginu (bez fotky) žádné těžké výpočty nedělal, jen lehké dotazování. Kolik CPU bere plugin v klidu v Logicu, jsem změřit nemohl. Je to krok 9 testu níže.

## 3. Proč čtení z fotky čte méně

Postavil jsem 0.9.0 (commit 7443e07) i 0.10.0 (6431418) z gitu. Všechny tři verze jsem spustil na **stejných souborech se stejnou pravdou** (sekce 6). Našel jsem čtyři příčiny:

1. **Hledání středu knobů (elysia):** 0.10 vždy hledala knob nejdřív ve velkém okolí a pak kolem společného posunu. Na skutečné fotce shora to pomohlo. U fotek zepředu ale střed knobu ujížděl a zářez se pak nepřečetl. Na pevné sadě (10 scén na přístroj) to vychází takto: **xmax 133 → 130, nvelope 156 → 153, xfilter ME 86 → 85**.
   - Oprava: společný posun se použije, jen když je opravdu znát (≥ 0,2 poloměru knobu). Jinak se hledá přesně jako ve 0.9.
2. **LED kroužky _RHEA:** pevný práh 55 % maxima a relativní jistota. V tečkovém režimu soused svítící LED chytí asi 50 % její záře, takže čtení vyšlo jako nejisté. Výsledek: **_RHEA 88 → 82**.
   - Oprava: rozhoduje největší skok jasu, který dává smysluplný vzor (tečka nebo pás od kraje či od středu).
3. **LED přepínače _RHEA:** práh jistoty se ve 0.10 zvedl z 0,30 na 0,35.
   - Oprava: zpět na 0,30.
4. **Výběr přístrojů po fotce celého racku** (jen v pluginu, testy 0.10 tuhle cestu vůbec neprocházely): přístroj se zahodil, když měl méně než polovinu výrazných bodů nejsilnějšího přístroje na fotce. Výrazné body (keypoints) jsou body potisku, podle kterých plugin přístroj na fotce najde. Měřením jsem doložil, že tím mizí správně nalezené přístroje: při 1440 px zahodil RHEA i nvelope (spravne 13/39 → 28/39 po opravě), při 4032 px zahodil xfilter.
   - Oprava: převezme se každý nalezený přístroj. Jen když dva najdou stejné místo na fotce (např. xfilter ME i 1U), zůstane silnější shoda.

**Proč nvelope v Logicu nemá fotku** (screenshot: „nvelope 500 + fotka“): na mé kopii tvé fotky se nvelope najde ve všech rozlišeních (1920, 2480, 3024 i 4032 px). Pravděpodobná příčina je filtr z bodu 4, ale **dokázané to nemám**. Fotka, kterou jsi v Logicu vybral, může mít jiné rozlišení nebo formát (HEIC).

Přidal jsem proto záznam `~/Music/Rack Memory/Reports/photo-analysis.log`. Pro každou analýzu je tam rozměr fotky, verze čtení, časy a u každého přístroje: nalezen ano/ne, body, přečteno, „z paměti“ a „překryv – nepoužito“. Když se to stane znovu, z jednoho řádku bude jasné proč.

## 4. Změněné soubory

| Soubor | Proč |
|---|---|
| `Source/PhotoWorker.h/.cpp` (nový) | jedno pracovní vlákno pro všechny fotky: nízká priorita, spí bez dotazování, novější fotka zruší starší úlohu, zavření okna analýzu zastaví a vlákno ukončí; záznam analýz do `Reports/photo-analysis.log` |
| `Source/Recognizer.h/.cpp` | zrušení analýzy uprostřed (kontrola mezi kroky); verze algoritmu `rmcv-0.11.0`; otisk fotky (MD5 obsahu); paměť výsledků (otisk + profil + verze, max. 32); jedna pracovní kopie fotky pro všechny přístroje a uvolnění po úloze; výběr přístrojů podle překryvu; opravy regrese (střed knobu, LED kroužek, LED přepínač) |
| `Source/PluginEditor.cpp/.h` | FOTO, přetažení fotky, ruční zarovnání i náhledy uložených fotek jdou přes `PhotoWorker`; přístroje se analyzují postupně (body fotky se počítají jednou); uložená fotka se dekóduje jednou na soubor; časovač okna: složka verzí po 5 s a jen když je okno vidět, Stažené po 2 min; měřič překresluje jen při změně; tlačítko „Převzít z fotky“; zápis otisku a verze do projektu |
| `Source/EditorParts.cpp` | Kontrola po fotce: řádek „fotka čte jinak než tvoje nastavení (ponecháno)“ a tlačítko **Převzít z fotky** |
| `Source/PluginProcessor.h/.cpp` | ruční nebo potvrzená hodnota se z fotky nepřepíše (jen návrh `photoProposals`); záznam ověřených příkladů `Corrections/corrections.jsonl`; `photoAnalysis` ve verzích |
| `Source/Model.h/.cpp` | `Snapshot::photoAnalysis` (otisk fotky, verze čtení, čas, přečteno); nepovinné pole, starší projekty se načtou beze změny; `sameReading` (tolerance porovnání) |
| `build.sh` | jen architektura tohoto Macu (univerzální: `--universal`); počet souběžných překladů = fyzická jádra − 1 (`RM_JOBS`); `nice`; složka buildu v `~/Library/Caches/RackMemoryBuild` (přežije `rm -rf RackMemory`); změněné soubory podle obsahu |
| `Tools/release.sh` | vydání dál univerzální (`RM_ARCHS="arm64;x86_64"`) |
| `CMakeLists.txt` | 0.11.0, `PhotoWorker.cpp` |
| `Tests/Tests.cpp`, `Tests/UiFlow.inc`, `Tests/Regress.inc` | nové testy (sekce 7), měření cestou tlačítka FOTO (`uiflow`), pevná regresní sada (`regress`) |
| `Tests/regression/` (nový) | 70 snímků + pravda + `manifest.json` (otisky, výsledky 0.9 / 0.10 / 0.11, základna) |

## 5. Výkon před a po

Analýza fotky celého racku (tvoje fotka 2480×3307), rack ze screenshotu (SPL, xfilter ME, RHEA, nvelope), stejná cesta jako tlačítko FOTO. Měřeno na Linuxu se 2 jádry:

| | 0.9 | 0.10 | **0.11** |
|---|---|---|---|
| čas analýzy | 7,5 s | 13,1 s | 16–17 s |
| CPU celkem | 12,6 s | 23,5 s | **15,8 s** (−33 % proti 0.10) |
| využitá jádra | 1,7 | 1,8 (všechna) | **1,0** |
| špička paměti | 0,80 GB | **1,77 GB** | **0,51 GB** (−71 %) |
| stejná fotka podruhé | znovu celá analýza | znovu celá analýza | **0,2 s** (z paměti) |
| zavření okna během analýzy | běží dál | běží dál | **vlákno skončí za 0,17 s** |
| pracovní vlákna po zavření | 0 až 5 doběhnou | 0 až 5 doběhnou | **0** (test) |

Na čtyřjádrovém Macu bude 0.11 o něco pomalejší než 0.10, protože počítá na jednom jádru místo všech. Zato se nepřehřívá a Logic má zbytek počítače pro sebe.

**Build:** změnu jsem nemohl změřit na Macu. Kolik práce odpadá, plyne přímo z build skriptu:
- **Architektury:** jen tento Mac místo dvou, tedy polovina práce.
- **Souběžné překlady:** fyzická jádra − 1 místo všech logických (na čtyřjádru 3 místo 8).
- **Další verze:** přeloží se jen změněné soubory (asi 10 z 63) místo všeho.

První build s novým skriptem přeloží všechno jednou, pro jednu architekturu. Skript na konci vypíše „Build trval X s“.

Nezměřil jsem na Macu: klidové CPU v Logicu, CPU a paměť při analýze na Macu, čas buildu. Postup měření je v sekci 10.

## 6. Čtení z fotky: před a po na stejných obrázcích

### Pevná regresní sada `Tests/regression`

Obsahuje 70 syntetických snímků (10 na přístroj), znám přesnou pravdu a soubory mají otisky MD5. Synteticky snímek je vykreslený panel přístroje se známým nastavením, posunutý, natočený a zašuměný. Hodnocení: knob správně do 9°, stupňový knob do 1 kroku, přepínač přesně.

| přístroj (ovladačů) | 0.9 správně / špatně | 0.10 | **0.11** |
|---|---|---|---|
| elysia nvelope 500 (160) | 156 / 0 | 153 / 0 | **156 / 0** |
| elysia xfilter 500 (140) | 134 / 0 | 135 / 0 | **134 / 0** |
| elysia xfilter ME (140) | 86 / 0 | 85 / 0 | **86 / 0** |
| elysia xmax 500 (150) | 133 / 0 | 130 / 0 | **133 / 0** |
| SPL Vitalizer (90) | 83 / **6** | 84 / 0 | **84 / 0** |
| WesAudio _RHEA (90) | 88 / 0 | 82 / 0 | **89 / 0** |
| **společné přístroje (770)** | **680 / 6** | 669 / 0 | **682 / 0** |
| elysia xfilter 1U (140, ve 0.9 není) | – | 79 / 0 | 79 / **1** |

- Proti 0.9 není 0.11 horší v žádném přístroji, který 0.9 znala. Proti 0.10 je horší v jediném čtení: xfilter 1U, syntetická scéna 8, High Mid Gain přečten o 15° vedle.
  - Zkoušel jsem dvě kontroly dvojznačnosti. Obě by tohle čtení odstranily jen za cenu 20 až 90 správných čtení jinde, tak jsem je nenechal. Je to v omezeních.

### Tvoje fotka celého racku

Pravda je odhad okem s tolerancí 20°:

| | 0.9 | 0.10 | **0.11** |
|---|---|---|---|
| 5 přístrojů na fotce: nalezeno | 2/5 | 5/5 | **5/5** |
| správně / 53 | 6 | 37 | **37** |
| špatně | 7 | 1 | **1** |
| tvůj rack (SPL, xfilter ME, RHEA, nvelope), cesta FOTO: převzato | 1/4 | 4/4 (v Logicu nvelope bez fotky) | **4/4** |
| totéž: správně / 39, špatně | 8 / 4 | 28 / 1 | **28 / 1** |
| totéž ve 1440 px: správně | 9 | 13 (zahodila RHEA i nvelope) | **27** |
| totéž ve 4032 px: převzato, správně, špatně | 1/4, 9, 4 | 3/4 (zahodila xfilter), 26, 1 | **4/4, 28, 2** (to druhé špatné: xfilter čtený profilem ME na panelu 1U) |

Jedno špatné čtení zůstává: nvelope Freq A vlevo, asi 25° od mého odhadu okem.

Na fotce je **xfilter 1U** (potisk ±16 dB). V racku máš pořád **ME** (screenshot), takže z xfilteru se čte jen 6–7 ovladačů a stupnice nesedí. Když jsou v racku oba profily, 0.11 sama vybere 1U (silnější shoda).

## 7. Testy

**Celkem 2230 kontrol OK, 0 selhalo.** Kompilace clang-18 je bez nových varování.

Nové kontroly:
- **Pracovní vlákno:**
  - nikdy dvě úlohy najednou;
  - pořadí úloh se drží;
  - vlákno vznikne až s první fotkou a po zavření skončí;
  - novější fotka pro stejný přístroj zruší starší úlohu, jiný přístroj se neruší, „celý rack“ zruší všechno.
- **Zrušení a zavření:**
  - zrušená analýza vrátí „zrušeno“ a nic nepřečte;
  - zavření během analýzy ukončí vlákno za 0,17 s.
- **Paměť výsledků:**
  - stejná fotka podruhé (i pod jiným jménem) se vrátí z paměti za méně než 50 ms se stejnými hodnotami;
  - zrušená analýza se do paměti neuloží;
  - opakovaná analýza bez paměti dá totéž, je deterministická.
- **Poškozená a malá fotka:** poškozený soubor, prázdný obrázek i obrázek 40×30 px skončí chybou a nic se nepřečte.
- **Výběr přístrojů:** sousední přístroje zůstanou oba, dva na stejném místě dají jen silnější, slabší jediný nalezený se neztratí.
- **Ochrana hodnot:**
  - ruční hodnota se z fotky nepřepíše, jen se nabídne;
  - „Převzít z fotky“ návrh převezme a jde vrátit Zpět;
  - nejisté čtení nic nemění, nezadaná hodnota se vyplní.
- **Ověřené příklady:**
  - potvrzení i oprava se zapíšou do `corrections.jsonl`;
  - stará ruční hodnota se jako oprava nepočítá;
  - nic se nezapíše dvakrát.
- **Projekt:**
  - otisk fotky a verze čtení přežijí uložení a otevření projektu;
  - starší stav bez nich se načte.
- **Editor:**
  - celý rack z fotky dvakrát: podruhé z paměti (16,9 s → 0,2 s), stejné hodnoty, jedno vlákno;
  - nová fotka během analýzy zruší starou;
  - zavření okna během analýzy nechá 0 vláken;
  - záznam v `photo-analysis.log`.
- **Regresní sada:** soubory se nezměnily (MD5) a žádný přístroj nesmí mít méně správně ani víc špatně než základna v `manifest.json`.

Měřicí režimy: `RackMemoryTests regress Tests/regression` a `RackMemoryTests uiflow <fotka> <truth.json> <profily…>` (stejná cesta jako FOTO).

## 8. Omezení a nevyřešené věci

- Proč nvelope v Logicu neměl fotku, jsem nedokázal. Rozhodne `photo-analysis.log` (sekce 3).
- V Logicu ani na Macu jsem nic neměřil ani nespouštěl (CPU, paměť, čas buildu). Postup je v sekci 10.
- Skutečná fotka je jen jedna a pravda je odhad okem. Syntetické snímky nenahrazují fotky z telefonu.
- xfilter 1U: jedno špatné čtení na syntetické sadě (sekce 6). xfilter (stříbrné knoby s malým zářezem) celkově přečte jen asi 55–60 %.
- _RHEA je na skutečné fotce ověřená jen se zhasnutými kroužky.
- Analýza na Macu potrvá déle než v 0.10 (jedno jádro), zato bez přehřívání.
- Opravy v `corrections.jsonl` se zatím k ničemu nepoužívají. Je to jen sběr ověřených příkladů, plugin se sám neučí.
- Univerzální balíček pro kamaráda s Apple Silicon: `bash RackMemory/build.sh --universal` nebo `Tools/release.sh`.

## 9. Sestavení a instalace

V Terminálu:

    cd ~/Downloads && rm -rf RackMemory && unzip -oq RackMemory-0.11.0.zip && bash RackMemory/build.sh

- Ještě šetrněji: `RM_JOBS=2 bash RackMemory/build.sh`.
- Na konci: `auval` PASS, „Build trval X s“, `Rack Memory 0.11.0 (x86_64).zip` ve Stažených.
- Pak restartuj Logic.

## 10. Test v Logicu

1. Otevři **Monitor činností** (Activity Monitor), záložka CPU, a vyhledej „Logic Pro“.
2. Vlož Rack Memory do projektu A a otevři okno.
3. V klidu (Logic stojí, okno otevřené) si 30 s všímej % CPU Logicu. Zapiš si to jako výchozí stav.
4. **FOTO** → fotka celého racku. Zapiš si:
   - jak dlouho analýza trvá a kolik % CPU Logic při ní bere (u jednoho jádra kolem 100 %, ne 400 %);
   - které přístroje dostaly fotku;
   - čísla v okně „Kontrola po fotce“.
5. Ještě jednou **FOTO** se stejnou fotkou. Výsledek musí být hned (z paměti) a hodnoty stejné.
6. Pravým tlačítkem na xfilter → **Nahradit jiným modelem** → **elysia xfilter (1U)**. Pak znovu FOTO. Xfilter by měl přečíst víc a se správnou stupnicí.
7. Ručně změň jeden knob (např. SPL Drive). Pak znovu FOTO. Knob se **nesmí přepsat**. V Kontrole po fotce bude „fotka čte jinak…“ a tlačítko **Převzít z fotky**.
8. Ulož projekt A, zavři ho, otevři projekt B s Rack Memory. B musí mít svoje nastavení, ne nastavení z A.
9. Znovu otevři A. Nastavení, ruční změny i fotky u přístrojů musí být, jak jsi je nechal.
10. Nech plugin 1 minutu v klidu. CPU Logicu se musí vrátit na výchozí stav z kroku 3.
11. Během analýzy (krok 4) zavři okno pluginu. CPU má do 1 s klesnout.
12. Zavři Logic. V Monitoru činností nesmí zůstat nic s „Logic“ ani „Rack Memory“.
13. Pošli mi soubor `~/Music/Rack Memory/Reports/photo-analysis.log`. Hlavně pokud nějaký přístroj fotku nedostal.

## 11. Co jsem vědomě odložil

- Stahování referenčních fotek z internetu (zadání: teď ne).
- Učení z oprav. Zatím jen sběr `corrections.jsonl`, nic se neučí samo.
- Nové přístroje a změny vzhledu.
- Upozornění „fotka víc odpovídá jinému modelu“, když je v racku jen jeden z podobných přístrojů (ME vs. 1U).
- Rychlejší analýza na víc jádrech s omezením paměti. Teď je bezpečně jedno jádro.
- Zlepšení čtení xfilteru (stříbrné knoby). Bez víc skutečných fotek zblízka by to bylo ladění naslepo.
