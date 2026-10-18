# MEMORY RACK 0.9.0 – opravy a spolehlivost (audit + výsledky)

Ověřeno na Linuxu: build, přes 1700 offline kontrol, 0 selhalo. **Na Macu v Logicu zatím nesestaveno** – postup testu je dole.

## A. Audit – co bylo rozbité a proč

| Problém | Skutečná příčina |
|---|---|
| Nastavení SPL se mezi projekty „ztrácí“ | Plugin při změně knobu hlásil jen `nonParameterStateChanged`. JUCE AU wrapper to Logicu **nepředává** (flags = 0 → žádný `PropertyChanged`). Logic proto nevěděl, že se projekt změnil, a zavřel / přepnul projekt **bez dotazu na uložení**. Samotné ukládání do projektu fungovalo (test: stav → nový plugin → stav sedí). |
| Projekty si „přepisují“ data | Data v projektu byla oddělená (klíč = instance), ale knihovna verzí je společná a bez skupin; navíc zkopírovaná stopa měla stejné `instanceId`. |
| Fotka RHEA nefunguje | Profil _RHEA neměl **žádné** čtení z fotky (`read` prázdné) → „fotka se jen uloží“. Nebyl to pád, funkce chyběla. |
| Okno se při zvětšení „nerozšíří“ | Rack se vešel buď na šířku, nebo na výšku; tah jen do šířky / jen do výšky nechal prázdné místo (rack byl omezený druhým rozměrem). |
| Nejistá hodnota z fotky smazala ruční hodnotu | `applyRecognition` u nejistého ovladače hodnotu mazal (rozpor se zadáním „zachovat ruční opravy“). |
| Aktualizace | Stahovala, ověřovala SHA a instalovala, ale „nainstalováno“ hlásila bez kontroly, co je opravdu na disku; neopakovala při výpadku sítě; neřešila kompatibilitu; knihovna hardwaru nebyla oddělená. |
| Patchbay „IN/OUT obráceně“ | V kódu žádný patchbay nebyl. Jediné IN/OUT (pruh hlasitosti) má IN první. Nový patchbay: řady podle výrobce, v nabídce „Přiřadit“ je vždy nejdřív IN, pak OUT. |

## B. Ukládání do projektu
- Skrytý, neautomatizovatelný parametr „Rack Memory state“: každá změna nastavení ho změní → **Logic ví, že je co uložit** (zeptá se při zavírání). Načtení projektu ho nemění (projekt se po otevření neoznačí jako změněný). Regresní test.
- Zkopírovaná stopa dostane novou identitu (žádné míchání záloh).
- **Obnova neuložených změn**: zavřeš-li projekt bez uložení, při dalším otevření TOHO projektu se nabídne „Obnovit / Zahodit“ (nikdy samo). Pozná správný projekt podle tokenu uloženého stavu – změny z „Uložit jako“ kopie se do originálu nenabídnou (test).
- Skupiny **Mixing / Mastering / vlastní**: každá má vlastní rack i vlastní verze (Mastering v1… / Mixing v1…). Načtení verze mění jen přístroje její skupiny.
- Verze: Uložit (vždy nová), Přepsat načtenou, Duplikovat, Přejmenovat, Porovnat (seznam rozdílů), Smazat (do koše).
- Presety přístrojů (globální, oddělené od skladeb): pravé tlačítko na přístroj.
- Fotka je jen odkaz: „Odebrat fotku“ nastavení nemaže (test). Nezadané ≠ 0 (test).
- Kopie racku (studia) jde s projektem – na Macu bez `rack.json` se obnoví. Knihovna nedostupná → verzi nejde uložit, ale projekt nastavení drží (test).
- Formát: snapshot schema 3, rack.json kompatibilní i se starší 0.8 (pole `devices`).

## C. Knoby, fotka, RHEA, okno
- Zřetelná ručička na každém knobu (černý obrys + bílá; šířka roste s knobem, nikdy se neztenčí). ••• → **Výrazné ukazatele** (žlutá, silnější).
- **Klik na knob = detail**: zvětšený ovladač, potisk, hodnota, poloha, stupnice, stav (ručně / z fotky ke kontrole / potvrzeno / preset / nezadáno), jistota, ± ladění, Napsat hodnotu, Nezadáno, Potvrdit; vedle výřez tvojí fotky.
- Tlačítko **FOTO** nahoře (i více fotek). Zprávy: „Fotka načtena – analyzuji…“, „Přístroj rozpoznán…“, „Některé ovladače potřebují kontrolu“, „Nastavení uloženo“.
- **Kontrola po fotce**: co se přečetlo, co je nejisté (klik = detail), Potvrdit vše.
- **Ruční zarovnání**: pravé tlačítko na přístroj → Zarovnat fotku ručně → 4 kliknutí na rohy.
- Fotku z dřívějška jde znovu otevřít (je-li v knihovně).
- **RHEA**: čte LED kroužky enkodérů (tečka i pás) a LED přepínačů; zarovnání podle potisku. Syntetické fotky 9/9 a 8/9, 0 špatně. **Skutečnou fotku RHEA jsem neměl** – nejisté zůstává ke kontrole.
- Okno drží poměr racku: tah do šířky = roste i výška, rack vyplní šířku (test).

## D. Gain
Gain je **digitální** gain uvnitř pluginu (před I/O pluginem → do interface → do hardwaru). Popisek „dB dig.“, parametr „Digital gain“ (ID beze změny, automatizace zůstává). Fyzický knob ani gain interface neovládá – XLR žádná data nenese. Zvuk se nemění (0 dB = bitově stejné).

## E. Patchbay a skupiny
Záložky: **PATCHBAY** · Mixing · Mastering · +. Patchbay: klik na jack a na druhý = kabel (zdroj → cíl), dvojklik = popis bodu, pravé tlačítko = přiřadit přístroj (IN, pak OUT), režim kanálu. Kabely jsou v projektu a ve verzích; popisky a režimy jsou společné (studio). Plugin nic nepřepíná, jen dokumentuje.

## F. Nové přístroje (vlastní kresby panelů, ne fotky výrobců)
- Warm Audio **WA76** (2U): INPUT, OUTPUT, ATTACK, RELEASE, RATIO 4/8/12/20/ALL, METER GR/+8/+4/OFF.
- Warm Audio **WA-2A** (2U): GAIN, PEAK REDUCTION, LIMIT/COMPRESS, METER GR/+10/+4.
- **Samson S-patch Plus**: 48 bodů, 1–24 / 25–48, přepínač N / HN / T na každém kanálu (manuál Samson).
- **Neutrik NYS-SPP-L1**: 48 bodů, 2 řady, half-normal z výroby, mění se otočením desky.
- Neověřené (v profilech jako otevřené otázky): čísla stupnic WA76/WA-2A (ukládá se poloha), přesné rozmístění, poloha přepínače LIMIT/COMPRESS, pre-emphasis WA-2A, režim Neutrik po otočení desky.

## G. Aktualizace
- Plugin: kontrola → dostupná → stahování (%) → ověřuji (velikost + SHA-256) → instaluji → **ověření verze na disku** → RESTARTUJ LOGIC. Stažené ≠ nainstalované (test). Stará verze se při chybě vrátí. Opakování při výpadku sítě. Jen odkazy na GitHub. `minimum_version` (jinak „nainstaluj ručně“), `macos_min`, architektura. Načtený plugin se nepřepisuje na místě (bundle se přesune, nový se nakopíruje).
- **Knihovna hardwaru** zvlášť: ••• → Zkontrolovat knihovnu hardwaru. Vydání `hardware` na GitHubu (`Tools/release_library.sh`), SHA-256, rozbalí jen `library.json` a `images/`, stará knihovna jako `Hardware.prev`. Profil se převezme jen s vyšší verzí a se všemi ID ovladačů.
- Nastavení na jednom místě: `Source/UpdateConfig.h` (+ `RM_UPDATE_REPO` v CMake).

## Test v Logicu (krok za krokem)
1. `cd ~/Downloads && rm -rf RackMemory && unzip -oq RackMemory-0.9.0.zip && bash RackMemory/build.sh`, restart Logicu.
2. Projekt A: Rack Memory na stopu, Mastering, nastav SPL DRIVE na 2:00. Zavři projekt → **Logic se musí zeptat na uložení** → Uložit.
3. Projekt B: nastav DRIVE 10:00, ulož. Otevři A → 2:00, B → 10:00, znovu A → 2:00.
4. V A změň knob a zavři BEZ uložení. Otevři A → žlutý pruh „Obnovit?“.
5. Záložka Mixing → + přístroj → WA-2A. Ulož (Mixing v1). Mastering verze se nezmění.
6. FOTO → fotka RHEA / SPL → kontrola po fotce → klik na nejistý ovladač → detail.
7. PATCHBAY → + patchbay → Samson → klik jack 1 nahoře, pak 3 dole = kabel.
8. Zvětši okno za roh do šířky → rack roste, žádné prázdné pruhy.
