#!/bin/sh
# Parte que necesita root: python-evdev, acceso a /dev/uinput y puerto en el firewall.
# La lanza instalar.sh con pkexec o sudo.
set -e
cd "$(dirname "$0")"
PUERTO="${1:-8765}"

if ! python3 -c 'import evdev' 2>/dev/null; then
  if command -v dnf >/dev/null; then dnf install -y python3-evdev
  elif command -v apt-get >/dev/null; then apt-get install -y python3-evdev
  elif command -v pacman >/dev/null; then pacman -S --needed --noconfirm python-evdev
  elif command -v zypper >/dev/null; then zypper install -y python3-evdev
  else echo "Instala python-evdev con el gestor de paquetes de tu distribución." >&2; exit 1
  fi
fi

install -m 644 70-uinput-apuntador.rules /etc/udev/rules.d/70-uinput-apuntador.rules
echo uinput > /etc/modules-load.d/uinput.conf
modprobe uinput
udevadm control --reload
udevadm trigger --name-match=uinput 2>/dev/null || udevadm trigger -s misc

if command -v firewall-cmd >/dev/null && systemctl is-active -q firewalld; then
  firewall-cmd --permanent --zone="$(firewall-cmd --get-default-zone)" --add-port="$PUERTO/tcp"
  firewall-cmd --reload
elif command -v ufw >/dev/null && ufw status | grep -q active; then
  ufw allow "$PUERTO/tcp"
fi
echo "Parte de root lista."
