#!/bin/bash
# Instala el servidor del apuntador para el usuario actual.
set -euo pipefail
DIR="$(dirname "$(readlink -f "$0")")"
PUERTO=8765
CONF="${XDG_CONFIG_HOME:-$HOME/.config}"
UNIDAD="$CONF/systemd/user/apuntador.service"

if [[ "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" == es* ]]; then ES=1; else ES=; fi
m() { if [[ -n "$ES" ]]; then printf '%s\n' "$1"; else printf '%s\n' "$2"; fi; }

m "→ Dependencias, /dev/uinput y firewall (pide contraseña)…" "→ Dependencies, /dev/uinput and firewall (asks for your password)…"
if command -v pkexec >/dev/null && [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
  pkexec "$DIR/instalar-root.sh" "$PUERTO"
else
  sudo "$DIR/instalar-root.sh" "$PUERTO"
fi

m "→ Servicio de usuario…" "→ User service…"
mkdir -p "$(dirname "$UNIDAD")" "$CONF/apuntador"
sed "s|@SERVIDOR@|$DIR/apuntador-servidor.py|" "$DIR/apuntador.service.in" > "$UNIDAD"
systemctl --user daemon-reload
systemctl --user enable apuntador.service
systemctl --user restart apuntador.service
sleep 2
systemctl --user is-active -q apuntador || { journalctl --user -u apuntador -n 20 --no-pager; exit 1; }

echo
m "Listo. Pega este token en los ajustes de la app (app Pebble → Apps → Apuntador → Settings):" \
  "Done. Paste this token into the app settings (Pebble app → Apps → Apuntador → Settings):"
echo
echo "    $(cat "$CONF/apuntador/token")"
echo
m "En cada red nueva ejecuta: $DIR/emparejar.sh" "On every new network run: $DIR/emparejar.sh"
