#!/usr/bin/python3
"""Genera los iconos de la app: menú del reloj (25×25, estilo de sistema) y tienda (80×80 y 144×144)."""
from pathlib import Path
from PIL import Image, ImageDraw

RAIZ = Path(__file__).resolve().parent.parent
NARANJA = (203, 75, 22, 255)    # Solarized orange
CREMA = (253, 246, 227, 255)    # Solarized base3
OSCURO = (0, 43, 54, 255)       # Solarized base03
ESCALA = 8                      # dibuja grande y reduce: bordes suaves


def dibujar(lado, fondo=True):
    s = lado * ESCALA
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if fondo:
        d.rounded_rectangle([0, 0, s - 1, s - 1], radius=s // 5, fill=NARANJA)
    m = s * 0.16 if fondo else s * 0.02
    # Pantalla de proyección con su pie
    arriba, abajo = s * 0.22, s * 0.66
    d.rounded_rectangle([m, arriba, s - m, abajo], radius=s // 24, fill=CREMA if fondo else OSCURO)
    d.rectangle([s * 0.47, abajo, s * 0.53, s * 0.80], fill=CREMA if fondo else OSCURO)
    d.rectangle([s * 0.32, s * 0.78, s * 0.68, s * 0.84], fill=CREMA if fondo else OSCURO)
    # Triángulo de "siguiente"
    cx, cy, r = s * 0.52, (arriba + abajo) / 2, (abajo - arriba) * 0.28
    d.polygon([(cx - r * 0.8, cy - r), (cx - r * 0.8, cy + r), (cx + r, cy)],
              fill=NARANJA if fondo else CREMA)
    return img.resize((lado, lado), Image.LANCZOS)


def icono_menu(lado=25):
    """Estilo de los iconos de PebbleOS: contorno negro de ~2 px sobre fondo
    transparente, sin antialias, para que el lanzador lo invierta al seleccionarlo."""
    E = ESCALA
    img = Image.new("RGBA", (lado * E, lado * E), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    negro = (0, 0, 0, 255)
    # Pantalla (contorno), pie y base, en coordenadas de píxel del icono final
    d.rounded_rectangle([1 * E, 3 * E, 24 * E - 1, 18 * E - 1], radius=2 * E, outline=negro, width=2 * E)
    d.rectangle([11.5 * E, 18 * E, 13.5 * E - 1, 22 * E - 1], fill=negro)
    d.rectangle([7 * E, 21 * E, 18 * E - 1, 23 * E - 1], fill=negro)
    # Triángulo de "siguiente" relleno
    d.polygon([(9.5 * E, 6.5 * E), (9.5 * E, 14.5 * E), (16.5 * E, 10.5 * E)], fill=negro)
    alfa = img.resize((lado, lado), Image.LANCZOS).getchannel("A").point(lambda a: 255 if a >= 110 else 0)
    out = Image.new("RGBA", (lado, lado), (0, 0, 0, 0))
    out.putalpha(alfa)
    return out


if __name__ == "__main__":
    icono_menu().save(RAIZ / "watchapp/resources/images/icono-menu.png")
    dibujar(80).save(RAIZ / "tienda/icono-80.png")
    dibujar(144).save(RAIZ / "tienda/icono-144.png")
    print("Iconos generados")
