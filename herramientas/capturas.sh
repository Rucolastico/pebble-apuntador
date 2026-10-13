#!/bin/bash
# Capturas de la tienda para todas las plataformas, con el estado de ejemplo
# del modo CAPTURAS de watchapp.c. Quedan en watchapp/screenshots/.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../watchapp"
C=src/c/watchapp.c
cp "$C" "$C.orig"
trap 'mv "$C.orig" "$C"; pebble clean >/dev/null; pebble build >/dev/null' EXIT
sed -i '1i #define CAPTURAS' "$C"
# waf no siempre nota el cambio de una línea: compilar desde cero.
pebble clean >/dev/null
pebble screenshot --all-platforms --no-open
