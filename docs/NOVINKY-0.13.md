# Rack Memory 0.13.0

6. 11. 2026

## 1. Aktualizace: tlačítko na hlavní stránce

Vpravo nahoře je teď vždy tlačítko s verzí:

| co ukazuje | znamená | klik |
|---|---|---|
| `v0.13.0 · zkontrolovat` | ještě se nekontrolovalo | zkontroluje GitHub hned |
| `v0.13.0 · hledám…` | probíhá kontrola | – |
| `v0.13.0 · aktuální ✓` | máš nejnovější | zkontroluje znovu |
| `v0.13.0 · offline` | GitHub nedostupný | zkusí znovu |
| `AKTUALIZACE 0.13.1 · 2 MB` (zelené) | je nová verze | stáhne, ověří, nainstaluje po zavření Logicu |

Postup instalace zůstává jako v 0.12: stažení s průběhem, ověření (SHA-256, verze, procesor), instalace až po zavření Logicu, záloha a automatický návrat při chybě.

## 2. Vydání na GitHubu automaticky

Nově vydání nedělá tvůj Mac. Dělá ho GitHub (`.github/workflows/release.yml`):

1. Claude zvýší verzi v `CMakeLists.txt` a pošle změny na GitHub.
2. GitHub na svém Macu postaví plugin pro Intel i Apple Silicon a zkontroluje verzi.
3. Spustí auval, zabalí zip a spočítá SHA-256.
4. Zveřejní vydání se souborem `latest.json`. Pluginy u všech pak nabídnou aktualizaci.

Pokud build selže, nic se nezveřejní. Log se uloží do neveřejného konceptu vydání, odkud si ho Claude přečte.

## 3. Ruční zadání hodnot, když je fotka nepřečte

Po fotce v panelu „Kontrola po fotce“:
- U každého přístroje je vidět „N nezadáno“.
- Tlačítko **Doplnit ručně** otevře psaní u prvního nezadaného ovladače.

Jak psát:
- Hodnotu piš tak, jak je na přístroji podle manuálu výrobce: `1 ms`, `10:1`, `60 Hz`, `16 kHz`, `Nuke`, `GR`, `+4`, číslo na stupnici (`7`), nebo polohu jako na hodinách (`2:30`).
- Tab skočí na další ovladač, Enter potvrdí, Esc zruší, `?` = nezadáno.

Psát jde kdykoli i jinak: dvojklikem na ovladač nebo v režimu `123`.

Oprava: text s jednotkou, kterou přepínač nemá (např. `7 ms` u API ATTACK), se odmítne. Dřív se mohl omylem vzít jako „poloha 7“.

## 4. Nový hardware (podle manuálů výrobců)

| přístroj | ovladače | zdroj |
|---|---|---|
| Universal Audio 1176LN (2U) | INPUT, OUTPUT, ATTACK 20–800 µs (zcela doleva = komprese vypnutá), RELEASE 50 ms–1,1 s, RATIO 4/8/12/20, METER GR/+8/+4/OFF | manuál UA 1176LN |
| Teletronix LA-2A (UA, 3U) | GAIN, PEAK REDUCTION, LIMIT/COMPRESS, METER (+10 / GR / +4) | manuál UA LA-2A |
| Empirical Labs Distressor EL8 (1U) | INPUT, ATTACK 50 µs–30 ms, RELEASE 0,05–3,5 s, OUTPUT (0–10½); RATIO 1:1…Nuke; DETECTOR HP/Band/Link; AUDIO HP + Dist 2/3; BYPASS | manuál Empirical Labs |
| API 2500+ (1U) | THRESH ±20 dBu, RATIO (7), ATTACK (7), RELEASE (6 + VAR 50 ms–3 s), KNEE, THRUST, TYPE, L/R LINK, SHAPE, MANUAL, GAIN 0–24 dB, BLEND, MIX, IN, BYP, METER | manuál API 2500+ |
| Warm Audio EQP-WA (2U) | LOW FREQ 20–800 Hz, BOOST 0–+12, ATTEN 0–−18, BANDWIDTH 0–10, HIGH BOOST 0–+18, HIGH FREQ 3–16 kHz, ATTEN 0–−14, ATTEN SEL 3–20 kHz, BYPASS | manuál Warm Audio |

Pravidla:
- Všechny rozsahy a polohy jsou přesně z manuálu.
- Co manuál neříká (čísla potisku, přesné rozmístění), si nevymýšlím. Ukládá se poloha a je to v „otevřených otázkách“ profilu.
- Kresby panelů jsou vlastní, ne fotografie.
- **Z fotky se tyto přístroje zatím nečtou.** Rozpoznávání potřebuje ověřený panel z fotky skutečného kusu. Hodnoty se nastavují myší nebo psaním.

## 5. Učení z fotek

Fotky z Instagramu ani z cizích webů nestahuju:
- je to proti pravidlům těch služeb a autorským právům,
- moje prostředí na ně ani nemá přístup.

Plugin se zpřesňuje na fotkách, které pošleš ty:
1. Ideálně z různých mobilů, i horší světlo.
2. U každé fotky potvrď nebo oprav ovladače jednotlivě.
3. `Tools/corrections_report.py --write` je přidá jako kandidáty do testovací sady.

Nové přístroje z tabulky výše se přidají do rozpoznávání, jakmile bude fotka skutečného kusu zepředu.
