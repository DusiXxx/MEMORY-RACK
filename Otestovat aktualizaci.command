#!/bin/bash
# Dvojklik: test aktualizace na tomto Macu (Logic musi byt zavreny). Vysledek v ~/Music/Rack Memory/Reports.
cd "$(dirname "$0")"
bash Tools/test_update_on_mac.sh "$@"
echo
read -n 1 -s -r -p "Hotovo - okno muzes zavrit (stiskni klavesu)."
