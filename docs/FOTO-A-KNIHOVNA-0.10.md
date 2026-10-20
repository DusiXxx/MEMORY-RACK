# MEMORY RACK 0.10.0 – čtení ze skutečné fotky, knihovna hardwaru, posouvání

Linux: build OK, **2156 offline kontrol, 0 selhalo**. Na Macu v Logicu zatím nesestaveno.

## 1. Audit – skutečné příčiny (fotka celého racku z telefonu, 20.10.2026)

| Problém | Příčina |
|---|---|
| xmax, nvelope, xfilter na fotce celého racku „nenalezeny“ | Výrazné body fotky se hledaly jen ve zmenšenině 1600 px. Modul 500 je pak malý a bodů je málo. Navíc geometricky ověřenou shodu porazila o chlup lepší (ale špatná) hypotéza podle jasu. |
| SPL: 4 knoby přečtené úplně špatně (i o 180°) | Knoby vyčnívají z panelu. Na fotce shora je čepička posunutá proti potisku a ryska se měřila od špatného středu. |
| elysia knoby na fotce shora | Vysoké knoby byly posunuté víc, než kam se hledalo (±0,45 poloměru). |
| RHEA: 3 hodnoty „přečtené“, i když LED nesvítily | Čtení LED bralo „jas × sytost“. Teplé světlo barví bílý panel do oranžova, takže panel vypadal jako svítící LED. |
| nvelope: svítící červené LED přečtené jako vypnuté | Pevný práh jasu nesedí na tmavou fotku. |
| xfilter | **Na tvé fotce je potisk standardního xfilteru (±16 / ±13 dB), ne Mastering Edition (±5 dB podle výrobce).** Přidal jsem profil „elysia xfilter (1U)“. |
| Okno na MacBooku nejde zvětšit | Okno drželo poměr racku. Výšku omezuje obrazovka, tím pádem i šířku. |

## 2. Co se změnilo v rozpoznávání
- Výrazné body se hledají dvakrát: nejdřív rychle (1600 px), a když přístroj nevyjde, znovu ve vysokém rozlišení (až 2800 px). Body fotky se počítají jednou a sdílejí se pro všechny přístroje.
- Geometricky ověřená shoda (≥ 12 bodů) má přednost.
- **SPL (ryska):** u každého knobu se najdou kandidáti rysky. Z os rysek všech knobů se spočítá společný posun čepiček (paralaxa). Pak se vezme ryska, jejíž osa míří do středu čepičky, a vnější konec určí směr.
- **elysia (zářez):** knoby se nejdřív hledají ve velkém okolí, pak se určí společný posun (medián) a potom jemně.
- **LED:** podle barvy LED z profilu (modrá, červená, žlutá, zelená), kontrast proti okolí, práh společný pro celý přístroj. Přepálený bílý střed svítící LED se počítá.
- **Stav každého ovladače:** přečteno / nejisté / mimo fotku / moc malé (vyfoť zblízka) / z fotky se nečte. Nejisté se nikdy nezmění na číslo; ruční hodnota zůstává.
- Knoby menší než 9 px se nečtou („vyfoť zblízka“).

### Měření (stejná fotka, pravda odečtená okem, tolerance 20°)

| | před (0.9) | teď (0.10) |
|---|---|---|
| nalezené přístroje | 2 / 5 | **5 / 5** |
| správně přečtené ovladače | 6 / 53 | **37 / 53** |
| špatně přečtené | 7 | **1** |
| nejisté (ponechané beze změny) | 40 | 15 |
| čas (5 přístrojů, Linux) | 14 s | 20 s |

Po přístrojích (teď): SPL 7/8, xmax 9/14, nvelope 12/15 (1 špatně: Freq A L 11:50 místo ~11:00), xfilter 2/8, RHEA 7/8 (kroužky nesvítí → správně „nezadáno“).
Zbylý chybný ovladač je hraniční (25° od mého odhadu). Test je v `Tests/photos_real/truth.json`, spuštění: `RackMemoryTests realphotos`.
Je to **jedna fotka** – na více fotkách z telefonu se přesnost může lišit.

## 3. Vrstva fotky
**FOTO VRSTVA** (vpravo nahoře): zarovnaná fotka přes kresbu přístroje, průhlednost posuvníkem. Nastavení se tím nemění (test).
V políčku přístroje je napsané „auto“ nebo „ručně zarovnáno“.
Pravým tlačítkem na přístroj: Zarovnat ručně (4 rohy), Znovu analyzovat uloženou fotku (= reset na automatické zarovnání), Odebrat fotku.
Zvětšení: klik na knob otevře detail i s výřezem fotky.

## 4. Knihovna hardwaru
- **+ PŘÍSTROJ** nahoře (a „+“ dole): hledání, filtr typu a formátu, náhled panelu, výrobce, formát, ověření, zdroje, otevřené otázky. Naposledy použité jsou nahoře.
- **Vlastní přístroj:** název, výrobce, formát, ovladače (řádek = ovladač, např. `RATIO | 1:1/2:1/4:1`). Panel se nakreslí schematicky. Uloží se do `~/Music/Rack Memory/UserHardware/`. Aktualizace knihovny ho nepřepíše.
- Duplikovat jako vlastní. Smazat vlastní: varuje, kolik verzí ho používá; nastavení zůstanou, soubor jde do koše.
- Pravým tlačítkem na přístroj:
  - **Přesunout do skupiny**
  - **Nahradit jiným modelem**: původní nastavení se uloží jako verze „před nahrazením“; polohy ovladačů se stejným ID se převezmou.
- Pravým tlačítkem na lunchbox → **Šasi**: Heritage Audio OST-10 v2.0 (10), API 500-6B/8B, Radial Workhorse, Rupert Neve R6, Cranborne 500R8.
- Skupiny jsou vlastní (+, přejmenovat, posunout, smazat). Mixing / Mastering jsou jen výchozí.

### Nové profily (vlastní kresby, ne fotky výrobců)
| Přístroj | Ověřeno | Neověřeno |
|---|---|---|
| elysia xfilter (1U) | potisk z fotky výrobce i tvé fotky | kroky čísel ±1 |
| API 512c (500, 1 slot) | ovladače podle manuálu (gain plynule 10–65 dB, Ø, 48V, PAD, MIC/HI-Z) | rozmístění, čísla stupnice |
| AMS Neve 1073LB (500, 1 slot) | 21 poloh zisku, TRIM −10…+5, LO Z, Ø, FR I/P, 48V | pořadí poloh při otáčení, rozmístění |
| Cranborne Camden 500 (500, 1 slot) | Mojo, Thump/Cream, HPF, Ø, Gain 12 poloh (Min, 2–11, Max), 48V, MIC/LINE/HI-Z | rozmístění |

Nepřidáno (nemám ověřené ovladače): API 550A (výrobce neuvádí přesné frekvence ani kroky), SSL 611EQ, Trident 80B, Drawmer. Dají se přidat jako vlastní přístroj.

## 5. Projekt a knihovna – tři vrstvy
1. Knihovna hardwaru: vestavěná + stažená + vlastní.
2. Rack, skupiny, patchbaye, presety (společné pro všechny projekty).
3. Projekt v Logicu: hodnoty, kabely a **otisk definic ovladačů** (verze profilu, počty poloh, stupnice).

Když se profil později změní nebo chybí, plugin to při otevření projektu napíše. Hodnoty zůstanou a nic se tiše nepřevykládá (test). Otisk je i v každé uložené verzi.

## 6. Okno
Rack vždy přes celou šířku. Co se nevejde na výšku, posouvá se (kolečko myši mimo knob, posuvník vpravo). Okno jde volně zvětšovat do šířky i výšky.
••• → „Celý rack do okna (bez posouvání)“ = původní chování.

## Test v Logicu
1. `cd ~/Downloads && rm -rf RackMemory && unzip -oq RackMemory-0.10.0.zip && bash RackMemory/build.sh`, restart Logicu.
2. FOTO → fotka celého racku → počkej ~30 s → **Kontrola po fotce**. Klikni na nejistý ovladač → detail → oprav nebo Potvrdit.
3. FOTO VRSTVA → posuvník průhlednosti. Sedí fotka na kresbě?
4. Pravým tlačítkem na xfilter → Nahradit jiným modelem → **elysia xfilter (1U)**. Polohy zůstanou, čísla budou ±16 dB.
5. Pravým tlačítkem na lunchbox → Šasi → Heritage Audio OST-10 v2.0.
6. + PŘÍSTROJ → hledej „neve“ → Přidat. Pak Vlastní přístroj (např. Distressor).
7. Roztáhni okno na MacBooku do šířky → rack roste, dolů se posouvá.
8. Ulož projekt, zavři, otevři → vše zůstane (stejně jako v 0.9).
