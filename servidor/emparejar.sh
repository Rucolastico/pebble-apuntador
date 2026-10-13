#!/bin/bash
# Prepara el apuntador en la red actual (p. ej. la del trabajo):
#   1. comprueba el servicio con una petición firmada,
#   2. averigua la IP de la computadora y la del teléfono (por adb, o la pide),
#   3. limita el servidor a ese teléfono (~/.config/apuntador/permitidas),
#   4. comprueba desde el teléfono que la computadora es alcanzable,
#   5. copia la IP de la computadora al portapapeles para añadirla en los ajustes.
# Uso: emparejar.sh [IP_DEL_TELEFONO]     emparejar.sh --abrir  (quita la lista)
set -euo pipefail

CONF="${XDG_CONFIG_HOME:-$HOME/.config}/apuntador"
PERMITIDAS="$CONF/permitidas"
PUERTO=8765

# Mensajes en español o inglés según el idioma del sistema.
if [[ "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" == es* ]]; then ES=1; else ES=; fi
m()     { if [[ -n "$ES" ]]; then printf '%s' "$1"; else printf '%s' "$2"; fi; }
ok()    { printf '\e[32m✔\e[0m %s\n' "$*"; }
aviso() { printf '\e[33m!\e[0m %s\n' "$*"; }
error() { printf '\e[31m✘\e[0m %s\n' "$*"; exit 1; }

if [[ "${1:-}" == "--abrir" || "${1:-}" == "--open" ]]; then
  rm -f "$PERMITIDAS"
  ok "$(m "Lista de IPs permitidas borrada: se acepta cualquier origen con firma válida." \
          "Allow list removed: any source with a valid signature is accepted.")"
  exit 0
fi

# Petición firmada como la del reloj; imprime el código HTTP.
peticion_firmada() {
  python3 - "$1" "$2" "$CONF/token" <<'EOF'
import hashlib, hmac, secrets, sys, time, urllib.request, urllib.error
url, ruta, fichero = sys.argv[1], sys.argv[2], sys.argv[3]
token = open(fichero).read().strip().encode()
t, n = str(int(time.time())), secrets.token_hex(12)
firma = hmac.new(token, f"GET {ruta} {t} {n}".encode(), hashlib.sha256).hexdigest()
req = urllib.request.Request(url + ruta, headers={"X-Tiempo": t, "X-Nonce": n, "X-Firma": firma})
try:
    print(urllib.request.urlopen(req, timeout=3).status)
except urllib.error.HTTPError as e:
    print(e.code)
except OSError:
    print("-")
EOF
}

# 1. Servicio
systemctl --user is-active -q apuntador || systemctl --user restart apuntador
sleep 1
codigo=$(peticion_firmada "http://127.0.0.1:$PUERTO" /ping)
[[ "$codigo" == 200 ]] || error "$(m "El servidor no responde bien a un ping firmado ($codigo). Mira: journalctl --user -u apuntador" \
                                     "The server does not answer a signed ping ($codigo). See: journalctl --user -u apuntador")"
ok "$(m "Servidor activo y firma correcta." "Server running, signature OK.")"

# 2. IP de la computadora en la red por la que sale a internet
read -r IP_PC DEV < <(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for(i=1;i<=NF;i++){if($i=="src")s=$(i+1);if($i=="dev")d=$(i+1)}} END{print s, d}')
[[ -n "${IP_PC:-}" && "$DEV" != tailscale* ]] || error "$(m "No encuentro la IP de la computadora en la red local. ¿Estás conectado a la Wi‑Fi?" \
                                                              "Cannot find this computer's local IP. Are you connected to Wi‑Fi?")"
RED=$(ip -4 -o addr show dev "$DEV" | awk '{print $4}' | head -1)
ok "$(m "Computadora: $IP_PC en $DEV (red $RED)." "Computer: $IP_PC on $DEV (network $RED).")"

# IP del teléfono: argumento, adb o a mano
IP_TELEFONO="${1:-}"
if [[ -z "$IP_TELEFONO" ]] && adb get-state >/dev/null 2>&1; then
  # Android no deja leer `ip addr` al usuario shell; el estado del Wi‑Fi sí lo trae.
  wifi=$(adb shell dumpsys connectivity 2>/dev/null | grep -m1 -oE 'SSID: "[^"]*".{0,160}IP: /[0-9.]+' || true)
  IP_TELEFONO=$(grep -oE '[0-9.]+$' <<<"$wifi" || true)
  SSID=$(grep -oE 'SSID: "[^"]*"' <<<"$wifi" | cut -d'"' -f2 || true)
  [[ -n "$IP_TELEFONO" ]] && ok "$(m "Teléfono (por adb): $IP_TELEFONO en la Wi‑Fi «$SSID»." "Phone (via adb): $IP_TELEFONO on Wi‑Fi “$SSID”.")"
fi
if [[ -z "$IP_TELEFONO" ]]; then
  aviso "$(m "No veo el teléfono por adb (conéctalo por USB o activa la depuración inalámbrica)." \
             "Phone not found via adb (plug it in over USB or enable wireless debugging).")"
  read -rp "  $(m "Escribe su IP (Ajustes → Red → Wi‑Fi → la red): " "Type its IP (Settings → Network → Wi‑Fi → the network): ")" IP_TELEFONO
fi
python3 -c "import ipaddress,sys; ipaddress.ip_address(sys.argv[1])" "$IP_TELEFONO" 2>/dev/null \
  || error "$(m "«$IP_TELEFONO» no es una IP válida." "“$IP_TELEFONO” is not a valid IP.")"
python3 -c "import ipaddress,sys; sys.exit(ipaddress.ip_address(sys.argv[1]) not in ipaddress.ip_network(sys.argv[2], strict=False))" "$IP_TELEFONO" "$RED" \
  || aviso "$(m "El teléfono ($IP_TELEFONO) no está en la red de la computadora ($RED). ¿Están en la misma Wi‑Fi?" \
                "The phone ($IP_TELEFONO) is not on this computer's network ($RED). Same Wi‑Fi?")"

# 3. Lista de permitidas: el teléfono en esta red sustituye a sus IP anteriores de la misma red
mkdir -p "$CONF"
python3 - "$PERMITIDAS" "$IP_TELEFONO" "$RED" <<'EOF'
import ipaddress, sys, datetime, pathlib
fichero, ip, red = pathlib.Path(sys.argv[1]), sys.argv[2], ipaddress.ip_network(sys.argv[3], strict=False)
lineas = fichero.read_text().splitlines() if fichero.exists() else [
    "# IPs allowed to send keys, besides 127.0.0.1 and Tailscale (100.64.0.0/10).",
    "# Maintained by emparejar.sh; one IP or network per line."]
def misma_red(linea):
    valor = linea.split("#", 1)[0].strip()
    try:
        return valor and ipaddress.ip_network(valor, strict=False).network_address in red
    except ValueError:
        return False
lineas = [l for l in lineas if not misma_red(l)]
lineas.append(f"{ip}  # phone on {red}, {datetime.date.today()}")
fichero.write_text("\n".join(lineas) + "\n")
EOF
ok "$(m "En esta red solo se aceptan teclas de $IP_TELEFONO (lista en $PERMITIDAS)." \
        "On this network only $IP_TELEFONO may send keys (list in $PERMITIDAS).")"

# 4. ¿El teléfono llega a la computadora? Sin firma debe contestar 401: si no contesta, la red lo bloquea.
if adb get-state >/dev/null 2>&1; then
  resp=$(adb shell "printf 'GET /ping HTTP/1.0\r\n\r\n' | toybox nc -w 3 $IP_PC $PUERTO" 2>/dev/null | head -1 || true)
  case "$resp" in
    *401*) ok "$(m "El teléfono alcanza la computadora en $IP_PC:$PUERTO." "The phone reaches this computer at $IP_PC:$PUERTO.")" ;;
    *403*) aviso "$(m "El teléfono llega, pero con otra IP que no está permitida. Ejecuta: $0 <IP_correcta>" \
                      "The phone gets through but with an IP that is not allowed. Run: $0 <correct_IP>")" ;;
    *)     aviso "$(m "El teléfono NO alcanza la computadora: la red seguramente aísla a los dispositivos entre sí." \
                      "The phone CANNOT reach this computer: the network probably isolates devices.")"
           echo "    $(m "Alternativa: comparte datos desde el teléfono (hotspot), conecta la computadora a él y vuelve a ejecutar este script." \
                         "Workaround: start a hotspot on the phone, connect the computer to it and run this script again.")" ;;
  esac
else
  aviso "$(m "Sin adb no puedo comprobar si el teléfono alcanza la computadora; abre la app en el reloj y fíjate en la vibración." \
             "Without adb I cannot check that the phone reaches this computer; open the watch app and feel the vibration.")"
fi

# 5. Lo que queda por hacer en el teléfono
if command -v wl-copy >/dev/null; then
  printf "%s" "$IP_PC" | wl-copy >/dev/null 2>&1 && ok "$(m "Copiada «$IP_PC» al portapapeles." "Copied “$IP_PC” to the clipboard.")"
fi
echo
m "En el teléfono: app Pebble → Apps → Apuntador → Settings → «Direcciones locales»,
añade $IP_PC (separada por coma de las que ya haya) y pulsa Guardar.
Al abrir la app en el reloj: una vibración corta = conectado.
" "On the phone: Pebble app → Apps → Apuntador → Settings → “Local addresses”,
add $IP_PC (comma separated) and tap Save.
When you open the watch app: one short vibration = connected.
"
