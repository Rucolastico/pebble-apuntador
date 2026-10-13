#!/usr/bin/python3
"""Genera los iconos de la app: menú del reloj (25×25) y tienda (80×80 y 144×144)."""
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


if __name__ == "__main__":
    dibujar(25).save(RAIZ / "watchapp/resources/images/icono-menu.png")
    dibujar(80).save(RAIZ / "tienda/icono-80.png")
    dibujar(144).save(RAIZ / "tienda/icono-144.png")
    print("Iconos generados")
