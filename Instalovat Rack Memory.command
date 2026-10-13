#!/bin/bash
cd "$(dirname "$0")"
bash ./build.sh
echo
read -n 1 -s -r -p "Hotovo - okno muzes zavrit (stiskni klavesu)."
