# Rack Memory 0.12.1: stav projektu a další krok

31. 10. 2026

## 1. Skutečný stav (audit)

| oblast | stav |
|---|---|
| kód | 0.12.0 na GitHubu (commit 12c7c74). Od té doby se nic nezměnilo, žádné cizí ani rozpracované změny |
| build u uživatele | 0.12.0 sestavená 23. 10. (`Rack Memory 0.12.0 (x86_64).zip`) |
| vydání na GitHubu | **pořád jen v0.8.0** (13. 10.). `latest.json` stažen 33×, zip 2×. Vydání 0.12 neproběhlo, takže updater zatím nemá co nabídnout a v praxi nikdy nestahoval |
| rozpoznávání na Macu (`photo-analysis.log`, 23. 10.) | fotka racku: SPL 8/9, xfilter ME 6/14, RHEA 1/9 (LED nesvítí – správně nezadáno), **nvelope nalezen 13/16**. Chyba z 0.10 (nvelope bez fotky) je pryč |
| Mac vs. Linux | stejná fotka: nvelope 13/16 na Macu, 14/16 v testech. Jiný kompilátor a procesor dávají drobný rozdíl. Testy v Linuxu proto nejsou přesný obraz Macu |
| opravy (`corrections.jsonl`) | 27 záznamů. Všechny vznikly „Potvrdit vše“ během jedné minuty. **Nejsou to ověřená data**: nikdo je neporovnal s přístrojem |
| rack | xfilter je v pluginu pořád jako **Mastering Edition**, i když na fotce je **1U** (potisk ±16 dB). Potvrzené hodnoty xfilteru jsou proto ve stupnici ME |
| „fotka“ 20. 10. | screenshot okna pluginu, ne hardware. Vyřazeno ze sady |
| sada fotek | 20 fotek + 42 variant, žádná nová. Z tohoto prostředí se na weby výrobců nedostanu; obcházet to nebudu |

Jeden konkrétní rozpor ke kontrole na přístroji:
- **nvelope, kanál 1, Freq A:** plugin čte asi −5° (krok uprostřed), já jsem z fotky okem odhadl asi −30°.
- Ty jsi to potvrdil hromadně.
- Prosím mrkni přímo na přístroj, kde ten knob stojí. Jedna ověřená hodnota rozhodne, jestli je chyba ve čtení, nebo v mém odhadu.

## 2. Co je nového v 0.12.1

### Aktualizace

Instalátor ukazuje průběh i se zavřeným Logicem, a to v notifikacích macOS:
1. „čeká – zavři Logic“
2. krok 2/5: kontrola balíčku
3. krok 3/5: záloha
4. krok 4/5: instalace
5. krok 5/5: ověření (auval)
6. „Hotovo – otevři Logic“, nebo „nepovedlo se, vrácena verze X“

Každé selhání v kterémkoli kroku se ohlásí notifikací.

Další změny:
- Plugin ukáže krok instalace, pokud běží jinde.
- **`Otestovat aktualizaci.command`** / `Tools/test_update_on_mac.sh` otestuje na Macu se skutečnými nástroji (ditto, PlistBuddy, lsof, auval):
  - instalaci,
  - vrácení předchozí verze,
  - auval,
  - zálohu,
  - že se **nesáhne na tvoji knihovnu** (verze skladeb, presety, rack, vlastní hardware, knihovna hardwaru, opravy, fotky).

  Bez parametru si testovací vyšší verzi vyrobí z nainstalovaného pluginu, takže se nic nestaví a Mac se nehřeje. Report uloží do `~/Music/Rack Memory/Reports/update-test-….txt`.

### Opravy jako ověřená data

Každé potvrzení nebo oprava se teď zapisuje s tím, jak silný je to důkaz:

| způsob | zápis | platí jako |
|---|---|---|
| ručně změněná hodnota | `user-corrected` | ověřená pravda |
| potvrzený jeden ovladač (klik na knob → Potvrdit) | `user-individual` | ověřená pravda |
| „Potvrdit vše“ | `user-bulk` | **neověřené**, do testů nesmí |

`Tools/corrections_report.py` udělá přehled oprav. Ukáže, jak často měla fotka pravdu a o kolik stupňů se mýlila. S `--write` přidá ověřené případy k fotce v katalogu jako **kandidáty** (`userTruth`). Testovací pravdou se stanou až po kontrole, plugin se nic neučí sám.

Tvých 27 záznamů tento nástroj vyhodnotil takto: 0 ověřených, 27 hromadných.

### Tak se plugin bude zlepšovat (bezpečně)

1. Vyfotíš přístroj a jednotlivě potvrdíš nebo opravíš ovladače. Klik na knob ukáže detail a výřez z fotky.
2. Ověřené opravy zkontroluješ (`corrections_report.py --write`) a fotka i pravda se přidají do testovací sady.
3. Každá změna rozpoznávání musí projít syntetickou sadou i sadou fotek bez zhoršení. Testy to hlídají.

## 3. Testy

- **Linux:** 2277 kontrol OK, 0 selhalo. Nové kontroly pokrývají notifikace (24 v testu), nezměněnou knihovnu po 5 instalacích a vráceních a sílu důkazu u oprav.
- **Syntetická sada:** 761/910, 1 špatně. **Sada fotek:** 72/83, 374/672, 8 špatně. Obojí beze změny, rozpoznávání se v této verzi neměnilo.
- **Mac:** neověřeno. Postup je níže.

## 4. Skutečný test přechodu na nové verze (na tvém Macu)

1. Build a vydání 0.12.1 na GitHub (vydání je univerzální, trvá déle; šetrněji s `RM_JOBS=2`):

       cd ~/Downloads && rm -rf RackMemory && unzip -oq RackMemory-0.12.1.zip
       RM_JOBS=2 bash RackMemory/Tools/release.sh "0.12.1: notifikace instalace, overene opravy"
       bash RackMemory/Tools/release.sh --verify

   S přihlášeným `gh` se vydá samo, jinak skript otevře stránku a řekne, které 2 soubory nahrát.

2. Test instalátoru se skutečnými nástroji. Zároveň tím vrátíš starší 0.12.0, aby měl updater co nabídnout (Logic zavřený):

       bash RackMemory/Tools/test_update_on_mac.sh ~/Downloads/"Rack Memory 0.12.0 (x86_64).zip"

3. Otevři Logic s 0.12.0 a projdi postup v pluginu:
   1. AKTUALIZACE 0.12.1 (velikost) → Stáhnout (procenta, rychlost, zkus i Zrušit).
   2. Počkej na „ověřuji“ a „INSTALOVAT“, potvrď.
   3. Zavři Logic a sleduj notifikace.
   4. Otevři Logic. Plugin má hlásit „Aktualizováno na 0.12.1“.
   5. Zkontroluj, že verze skladeb, presety a rack zůstaly.

4. Pošli `~/Music/Rack Memory/Reports/update-test-*.txt` a `Updates/install.log`. Jsou ve složce, kterou už mám povolenou, takže si je můžu přečíst sám.

## 5. Co zbývá

- Skutečné vydání a test v Logicu (sekce 4). Bez Macu to neudělám.
- Ověřené hodnoty z přístroje. Zatím 0. Nejrychlejší cesta: 2 až 3 fotky zblízka a pro každou jednotlivě potvrdit nebo opravit ovladače.
- Čtení zářezů na stříbrných knobech elysia (8 chybných čtení v sadě fotek). Zlepšovat ho má smysl až s ověřenými fotkami.
- Další modely v knihovně: zadání se useklo v bodě 4 („Kvalitní knihovnu anal…“).
