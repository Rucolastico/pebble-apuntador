#!/usr/bin/python3
"""Servidor del apuntador Pebble: recibe peticiones HTTP y simula teclas vía uinput.

Endpoints:
  GET  /ping                   -> {"ok": true, "host": ...}
  POST /key/<perfil>/<accion>  -> pulsa la tecla de esa acción en ese perfil
  POST /key/<accion>           -> igual, con el perfil "estandar"
Acciones: next, prev, black, start, start_here, esc.
Perfiles: estandar (PowerPoint, LibreOffice Impress) y onlyoffice.

Cada petición va firmada; el token nunca viaja por la red. Cabeceras:
  X-Tiempo  segundos Unix (se admite un desfase de ±VENTANA s)
  X-Nonce   valor aleatorio de un solo uso
  X-Firma   hex(HMAC-SHA256(token, "MÉTODO ruta tiempo nonce"))
Si existe ~/.config/apuntador/permitidas (una IP o red por línea, # comenta),
solo se aceptan peticiones de esas direcciones, además de 127.0.0.1 y Tailscale.

El token se lee de ~/.config/apuntador/token; si no existe se genera.
Las teclas pueden cambiarse por perfil en ~/.config/apuntador/teclas.json, p. ej.
  {"estandar": {"black": ["W"]}}
con nombres de evdev sin el prefijo KEY_ (la última es la tecla, las demás modificadores).
"""

import hashlib
import hmac
import ipaddress
import json
import os
import secrets
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from evdev import AbsInfo, UInput, ecodes as e

PUERTO = int(os.environ.get("APUNTADOR_PUERTO", "8765"))
DIR_CONFIG = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "apuntador"
FICHERO_TOKEN = DIR_CONFIG / "token"
FICHERO_TECLAS = DIR_CONFIG / "teclas.json"
FICHERO_PERMITIDAS = DIR_CONFIG / "permitidas"
VENTANA = 30
SIEMPRE_PERMITIDAS = [ipaddress.ip_network("127.0.0.0/8"), ipaddress.ip_network("100.64.0.0/10")]
SCRIPT_KWIN = Path(__file__).resolve().with_name("ventana-activa.js")
ABS_MAX = 32767

# perfil -> acción -> (modificadores, tecla)
_COMUNES = {
    "next": ((), e.KEY_RIGHT),
    "prev": ((), e.KEY_LEFT),
    "esc": ((), e.KEY_ESC),
}
PERFILES = {
    # PowerPoint y LibreOffice Impress comparten atajos.
    "estandar": dict(_COMUNES, black=((), e.KEY_B), start=((), e.KEY_F5),
                     start_here=((e.KEY_LEFTSHIFT,), e.KEY_F5)),
    # OnlyOffice inicia con Ctrl+F5, no tiene pantalla negra ni "desde la actual":
    # se usan Tab e Inicio (primera diapositiva) en su lugar.
    "onlyoffice": dict(_COMUNES, black=((), e.KEY_TAB), start=((e.KEY_LEFTCTRL,), e.KEY_F5),
                       start_here=((), e.KEY_HOME)),
}


def cargar_teclas():
    if not FICHERO_TECLAS.exists():
        return
    for perfil, acciones in json.loads(FICHERO_TECLAS.read_text()).items():
        if perfil not in PERFILES or not isinstance(acciones, dict):
            sys.exit(f"{FICHERO_TECLAS}: perfil desconocido «{perfil}» (usa {', '.join(PERFILES)})")
        for accion, nombres in acciones.items():
            if accion not in PERFILES[perfil]:
                sys.exit(f"{FICHERO_TECLAS}: acción desconocida «{accion}»")
            codigos = []
            for n in nombres:
                codigo = getattr(e, "KEY_" + n.upper(), None)
                if codigo is None:
                    sys.exit(f"{FICHERO_TECLAS}: tecla desconocida «{n}»")
                codigos.append(codigo)
            PERFILES[perfil][accion] = (tuple(codigos[:-1]), codigos[-1])
            print(f"{perfil}/{accion} -> {'+'.join(nombres)}", flush=True)


def cargar_permitidas():
    """Redes permitidas, o None si no hay lista (se acepta cualquier origen)."""
    if not FICHERO_PERMITIDAS.exists():
        return None
    redes = list(SIEMPRE_PERMITIDAS)
    for linea in FICHERO_PERMITIDAS.read_text().splitlines():
        linea = linea.split("#", 1)[0].strip()
        if linea:
            redes.append(ipaddress.ip_network(linea, strict=False))
    return redes


def cargar_token():
    if not FICHERO_TOKEN.exists():
        DIR_CONFIG.mkdir(parents=True, exist_ok=True)
        fd = os.open(FICHERO_TOKEN, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(secrets.token_urlsafe(24) + "\n")
        print(f"Token nuevo generado en {FICHERO_TOKEN}", flush=True)
    token = FICHERO_TOKEN.read_text().strip()
    if len(token) < 16:
        sys.exit(f"El token de {FICHERO_TOKEN} es demasiado corto (mínimo 16 caracteres)")
    return token.encode()


class Teclado:
    def __init__(self):
        combinaciones = [c for acciones in PERFILES.values() for c in acciones.values()]
        teclas = {t for _, t in combinaciones} | {m for mods, _ in combinaciones for m in mods}
        self.ui = UInput({e.EV_KEY: sorted(teclas)}, name="Apuntador Pebble")
        # Puntero absoluto (0..ABS_MAX sobre todo el escritorio) para dar el foco con un clic.
        absinfo = AbsInfo(0, 0, ABS_MAX, 0, 0, 0)
        self.puntero = UInput({e.EV_KEY: [e.BTN_LEFT], e.EV_ABS: [(e.ABS_X, absinfo), (e.ABS_Y, absinfo)]},
                              name="Apuntador Pebble (puntero)")
        self.lock = threading.Lock()

    def clic(self, x, y, escritorio):
        ex, ey, ancho, alto = escritorio
        with self.lock:
            self.puntero.write(e.EV_ABS, e.ABS_X, (x - ex) * ABS_MAX // max(ancho - 1, 1))
            self.puntero.write(e.EV_ABS, e.ABS_Y, (y - ey) * ABS_MAX // max(alto - 1, 1))
            self.puntero.syn()
            time.sleep(0.05)
            self.puntero.write(e.EV_KEY, e.BTN_LEFT, 1)
            self.puntero.syn()
            time.sleep(0.03)
            self.puntero.write(e.EV_KEY, e.BTN_LEFT, 0)
            self.puntero.syn()

    def pulsar(self, mods, tecla):
        with self.lock:
            for m in mods:
                self.ui.write(e.EV_KEY, m, 1)
            self.ui.write(e.EV_KEY, tecla, 1)
            self.ui.syn()
            time.sleep(0.02)
            self.ui.write(e.EV_KEY, tecla, 0)
            for m in reversed(mods):
                self.ui.write(e.EV_KEY, m, 0)
            self.ui.syn()


def ventana_activa():
    """Pregunta a KWin por la ventana activa; devuelve un dict o None."""
    nombre = f"apuntador-{os.getpid()}"
    monitor = subprocess.Popen(["dbus-monitor", "interface='org.apuntador.Ventana'"],
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    try:
        time.sleep(0.3)  # que dbus-monitor esté escuchando antes de lanzar el script
        qdbus = ["qdbus-qt6", "org.kde.KWin"]
        n = subprocess.run(qdbus + ["/Scripting", "org.kde.kwin.Scripting.loadScript", str(SCRIPT_KWIN), nombre],
                           capture_output=True, text=True, timeout=3).stdout.strip()
        subprocess.run(qdbus + [f"/Scripting/Script{n}", "org.kde.kwin.Script.run"], capture_output=True, timeout=3)
        time.sleep(0.3)
        subprocess.run(qdbus + ["/Scripting", "org.kde.kwin.Scripting.unloadScript", nombre], capture_output=True, timeout=3)
    except (OSError, subprocess.SubprocessError) as error:
        print(f"No se pudo consultar a KWin: {error}", flush=True)
    finally:
        monitor.terminate()
    salida = monitor.communicate()[0]
    for linea in salida.splitlines():
        linea = linea.strip()
        if linea.startswith('string "{'):
            return json.loads(linea[len('string "'):-1].replace('\\"', '"'))
    return None


def enfocar_presentacion(teclado):
    """OnlyOffice abre la presentación sin darle el foco del teclado: las flechas no
    llegan hasta que se hace clic en ella. El clic avanza una diapositiva, así que
    después se manda ← para volver a la primera."""
    time.sleep(1.2)  # lo que tarda en aparecer la presentación
    info = ventana_activa()
    if not info or "onlyoffice" not in info.get("clase", "").lower() or not info.get("completa"):
        print(f"Sin presentación de OnlyOffice a pantalla completa: {info}", flush=True)
        return
    x, y, ancho, alto = info["geo"]
    teclado.clic(x + 2, y + alto // 2, info["escritorio"])  # borde izquierdo, lejos de enlaces
    time.sleep(0.5)
    teclado.pulsar((), e.KEY_LEFT)
    print("Foco dado a la presentación de OnlyOffice", flush=True)


class Manejador(BaseHTTPRequestHandler):
    server_version = "Apuntador/1"
    sys_version = ""

    def _responder(self, codigo, cuerpo):
        datos = json.dumps(cuerpo).encode()
        self.send_response(codigo)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(datos)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(datos)

    def _origen_permitido(self):
        # Se relee en cada petición para que emparejar.sh no tenga que reiniciar el servicio.
        redes = cargar_permitidas()
        if redes is None:
            return True
        ip = ipaddress.ip_address(self.client_address[0])
        return any(ip in red for red in redes)

    def _firma_valida(self, ruta):
        tiempo = self.headers.get("X-Tiempo", "")
        nonce = self.headers.get("X-Nonce", "")
        firma = self.headers.get("X-Firma", "")
        if not tiempo.isdigit() or not 8 <= len(nonce) <= 64 or not firma:
            return False
        ahora = time.time()
        if abs(ahora - int(tiempo)) > VENTANA:
            return False
        mensaje = f"{self.command} {ruta} {tiempo} {nonce}".encode()
        esperada = hmac.new(self.server.token, mensaje, hashlib.sha256).hexdigest()
        if not hmac.compare_digest(firma.lower().encode(), esperada.encode()):
            return False
        # Una petición capturada no puede reenviarse: cada nonce vale una sola vez.
        with self.server.lock_nonces:
            usados = self.server.nonces
            for n in [n for n, caduca in usados.items() if caduca < ahora]:
                del usados[n]
            if nonce in usados:
                return False
            usados[nonce] = ahora + 2 * VENTANA
        return True

    def _atender(self):
        ruta = self.path.split("?", 1)[0].rstrip("/")
        if not self._origen_permitido():
            self._responder(403, {"ok": False, "error": "origen"})
            return
        if not self._firma_valida(ruta):
            time.sleep(1)  # frena a quien pruebe a lo bruto
            self._responder(401, {"ok": False, "error": "firma"})
            return
        if ruta == "/ping":
            self._responder(200, {"ok": True, "host": socket.gethostname()})
        elif ruta.startswith("/key/"):
            partes = ruta[5:].split("/")
            perfil, accion = partes if len(partes) == 2 else ("estandar", partes[0])
            teclas = PERFILES.get(perfil, {}).get(accion)
            if teclas is None:
                self._responder(404, {"ok": False, "error": "accion"})
                return
            self.server.teclado.pulsar(*teclas)
            self._responder(200, {"ok": True, "perfil": perfil, "accion": accion})
            if perfil == "onlyoffice" and accion == "start":
                threading.Thread(target=enfocar_presentacion, args=(self.server.teclado,), daemon=True).start()
        else:
            self._responder(404, {"ok": False, "error": "ruta"})

    def do_GET(self):
        self._atender()

    def do_POST(self):
        self._atender()

    def log_message(self, formato, *args):
        print(f"{self.client_address[0]} {formato % args}", flush=True)


def main():
    servidor = ThreadingHTTPServer(("0.0.0.0", PUERTO), Manejador)
    servidor.daemon_threads = True
    servidor.token = cargar_token()
    servidor.nonces = {}
    servidor.lock_nonces = threading.Lock()
    cargar_teclas()
    servidor.teclado = Teclado()
    print(f"Apuntador escuchando en el puerto {PUERTO}", flush=True)
    servidor.serve_forever()


if __name__ == "__main__":
    main()
