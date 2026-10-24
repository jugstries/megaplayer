#!/usr/bin/env python3
"""Disegna le icone dell'app, una per skin, e le scrive nel catalogo di macOS.

La moderna è la manopola MegaSound sul fondo scuro; la retrò è la mascotte
floppy della finestra "Ciao!" sul grigio della scrivania. Le icone seguono la sagoma di
macOS (un quadrato arrotondato di 824 punti su 1024, con l'ombra), così nel
Dock stanno in fila con le altre.

Il bundle porta la moderna (AppIcon); la retrò è un'immagine a parte
(RetroIcon) che l'app mette nel Dock quando si passa alla skin retrò.

Uso: python3 tool/make_icons.py [--preview DIR]   (serve resvg nel PATH)
"""

import argparse
import json
import math
import pathlib
import subprocess
import tempfile

APP = pathlib.Path(__file__).resolve().parent.parent
ASSETS = APP / "macos/Runner/Assets.xcassets"

SIZE = 1024
INSET = 100  # 1024 - 824, diviso due
SHAPE = SIZE - 2 * INSET

# Colori delle skin (lib/skins/modern, lib/skins/retro).
BACKGROUND = "#0C0B10"
SURFACE = "#15131B"
BORDER = "#2A2633"
TRACK = "#34303F"
TEXT = "#F2EFF7"
ORANGE = (0xFF, 0x8A, 0x3D)
PINK = (0xEC, 0x4F, 0x8F)
VIOLET = (0x8B, 0x5C, 0xF6)
DESK = "#8C99AA"
INK = "#111111"
PAPER = "#FFFFFF"
LILAC = "#B8A9E8"
RETRO_PINK = "#F3A6C8"


def squircle_path():
    """La sagoma delle icone di macOS: un quadrato con gli angoli continui
    (raggio 185,4 e smussatura al 60%, come gli angoli "smoothed" di Figma)."""
    radius, smoothing = 185.4, 0.6
    p = (1 + smoothing) * radius
    arc_measure = 90 * (1 - smoothing)
    arc_length = math.sin(math.radians(arc_measure / 2)) * radius * math.sqrt(2)
    alpha = (90 - arc_measure) / 2
    p3_to_p4 = radius * math.tan(math.radians(alpha / 2))
    beta = 45 * smoothing
    c = p3_to_p4 * math.cos(math.radians(beta))
    d = c * math.tan(math.radians(beta))
    b = (p - arc_length - c - d) / 3
    a = 2 * b
    r = radius
    lo, hi = INSET, INSET + SHAPE
    f = lambda *values: " ".join(f"{v:.2f}" for v in values)
    return (
        f"M{f(lo + p, lo)} H{hi - p:.2f}"
        f" c{f(a, 0, a + b, 0, a + b + c, d)} a{f(r, r)} 0 0 1 {f(arc_length, arc_length)}"
        f" c{f(d, c, d, b + c, d, a + b + c)} V{hi - p:.2f}"
        f" c{f(0, a, 0, a + b, -d, a + b + c)} a{f(r, r)} 0 0 1 {f(-arc_length, arc_length)}"
        f" c{f(-c, d, -(b + c), d, -(a + b + c), d)} H{lo + p:.2f}"
        f" c{f(-a, 0, -(a + b), 0, -(a + b + c), -d)} a{f(r, r)} 0 0 1 {f(-arc_length, -arc_length)}"
        f" c{f(-d, -c, -d, -(b + c), -d, -(a + b + c))} V{lo + p:.2f}"
        f" c{f(0, -a, 0, -(a + b), d, -(a + b + c))} a{f(r, r)} 0 0 1 {f(arc_length, -arc_length)}"
        f" c{f(c, -d, b + c, -d, a + b + c, -d)} Z"
    )


def frame(defs, body):
    """Sagoma con l'ombra di sistema; `body` è disegnato dentro la sagoma."""
    shape = squircle_path()
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {SIZE} {SIZE}">
<defs>
  <clipPath id="shape"><path d="{shape}"/></clipPath>
  <filter id="drop" x="-10%" y="-10%" width="120%" height="125%">
    <feDropShadow dx="0" dy="10" stdDeviation="12" flood-color="#000" flood-opacity="0.35"/>
  </filter>
{defs}
</defs>
<path d="{shape}" fill="#000" filter="url(#drop)"/>
<g clip-path="url(#shape)">
{body}
</g>
</svg>
"""


def hex_colour(rgb):
    return "#" + "".join(f"{round(x):02X}" for x in rgb)


def mix(a, b, t):
    return hex_colour(x + (y - x) * t for x, y in zip(a, b))


def sweep(t):
    """Il gradiente della skin moderna: arancio, rosa, viola."""
    return mix(ORANGE, PINK, t * 2) if t < 0.5 else mix(PINK, VIOLET, (t - 0.5) * 2)


def polar(cx, cy, r, degrees):
    a = math.radians(degrees)
    return cx + r * math.cos(a), cy + r * math.sin(a)


def arc(cx, cy, r, start, end):
    x0, y0 = polar(cx, cy, r, start)
    x1, y1 = polar(cx, cy, r, end)
    large = 1 if end - start > 180 else 0
    return f"M{x0:.2f} {y0:.2f} A{r} {r} 0 {large} 1 {x1:.2f} {y1:.2f}"


def modern_svg():
    cx = cy = SIZE / 2
    radius, width = 262, 58
    start, span, value = 135, 270, 0.82  # come la manopola: da sud-ovest a sud-est
    defs = f"""  <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#1D1A26"/><stop offset="1" stop-color="{BACKGROUND}"/>
  </linearGradient>
  <radialGradient id="violet" cx="0.82" cy="0.14" r="0.6">
    <stop offset="0" stop-color="{hex_colour(VIOLET)}" stop-opacity="0.55"/>
    <stop offset="1" stop-color="{hex_colour(VIOLET)}" stop-opacity="0"/>
  </radialGradient>
  <radialGradient id="orange" cx="0.16" cy="0.9" r="0.55">
    <stop offset="0" stop-color="{hex_colour(ORANGE)}" stop-opacity="0.32"/>
    <stop offset="1" stop-color="{hex_colour(ORANGE)}" stop-opacity="0"/>
  </radialGradient>
  <linearGradient id="wave" x1="0" y1="0" x2="1" y2="0">
    <stop offset="0" stop-color="{sweep(0)}"/><stop offset="0.5" stop-color="{sweep(0.5)}"/>
    <stop offset="1" stop-color="{sweep(1)}"/>
  </linearGradient>
  <filter id="glow" x="-30%" y="-30%" width="160%" height="160%">
    <feGaussianBlur stdDeviation="18"/>
  </filter>
  <filter id="face" x="-20%" y="-20%" width="140%" height="150%">
    <feDropShadow dx="0" dy="16" stdDeviation="20" flood-color="#000" flood-opacity="0.55"/>
  </filter>"""

    parts = [
        f'<rect width="{SIZE}" height="{SIZE}" fill="url(#bg)"/>',
        f'<rect width="{SIZE}" height="{SIZE}" fill="url(#violet)"/>',
        f'<rect width="{SIZE}" height="{SIZE}" fill="url(#orange)"/>',
        f'<path d="{squircle_path()}" fill="none" stroke="#FFFFFF" stroke-opacity="0.10" stroke-width="6"/>',
        # Il binario della manopola, poi il valore a spicchi con il colore
        # che gira (SVG non ha gradienti conici).
        f'<path d="{arc(cx, cy, radius, start, start + span)}" fill="none" stroke="{TRACK}" '
        f'stroke-width="{width}" stroke-linecap="round"/>',
    ]
    end = start + span * value
    steps = 120
    glow = []
    for i in range(steps):
        a0 = start + (end - start) * i / steps
        a1 = start + (end - start) * (i + 1) / steps + 0.6
        colour = sweep((i + 0.5) / steps)
        segment = f'<path d="{arc(cx, cy, radius, a0, min(a1, end))}" fill="none" stroke="{colour}" stroke-width="{width}"/>'
        parts.append(segment)
        glow.append(segment)
    for angle, t in ((start, 0), (end, 1)):
        x, y = polar(cx, cy, radius, angle)
        cap = f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{width / 2}" fill="{sweep(t)}"/>'
        parts.append(cap)
        glow.append(cap)
    parts.insert(5, f'<g filter="url(#glow)" opacity="0.55">{"".join(glow)}</g>')
    x, y = polar(cx, cy, radius, end)
    parts.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="15" fill="#FFFFFF"/>')

    # La faccia della manopola, con dentro l'oscilloscopio.
    face = radius - width / 2 - 30
    parts.append(f'<circle cx="{cx}" cy="{cy}" r="{face}" fill="{SURFACE}" stroke="{BORDER}" '
                 f'stroke-width="5" filter="url(#face)"/>')
    points = []
    left, right = cx - face * 0.68, cx + face * 0.68
    for i in range(241):
        t = i / 240
        envelope = math.sin(math.pi * t) ** 1.6
        y = cy - face * 0.42 * envelope * math.sin(2 * math.pi * 2.5 * t)
        points.append(f"{left + (right - left) * t:.1f} {y:.1f}")
    wave = "M" + " L".join(points)
    parts.append(f'<path d="{wave}" fill="none" stroke="url(#wave)" stroke-width="40" stroke-linecap="round" '
                 f'stroke-linejoin="round" opacity="0.35" filter="url(#glow)"/>')
    parts.append(f'<path d="{wave}" fill="none" stroke="{TEXT}" stroke-width="22" stroke-linecap="round" '
                 f'stroke-linejoin="round"/>')
    return frame(defs, "\n".join(parts))


# La mascotte della finestra "Ciao!" (lib/skins/retro/retro_skin.dart).
MASCOT = [
    "##############..",
    "#llll#####llll#.",
    "#llll#w#w#llll##",
    "#llll#w#w#lllll#",
    "#llll#####lllll#",
    "#llllllllllllll#",
    "#l############l#",
    "#l#wwwwwwwwww#l#",
    "#l#wwwwwwwwww#l#",
    "#l#ww#wwww#ww#l#",
    "#l#ww#wwww#ww#l#",
    "#l#wpwwwwwwpw#l#",
    "#l#wwww##wwww#l#",
    "#l#wwwwwwwwww#l#",
    "#l############l#",
    "################",
]
MASCOT_COLOURS = {"#": INK, "l": LILAC, "w": PAPER, "p": RETRO_PINK}


def retro_svg():
    unit = 32  # 16 pixel della mascotte: 512 su 1024, 16 a 512
    origin = (SIZE - unit * 17) // 2  # mascotte più l'ombra di un pixel
    parts = [f'<rect width="{SIZE}" height="{SIZE}" fill="{DESK}"/>']
    pixels = []
    # L'ombra netta delle finestre della skin, spostata di un pixel.
    for y, row in enumerate(MASCOT):
        for x, cell in enumerate(row):
            if cell != ".":
                pixels.append(f'<rect x="{origin + (x + 1) * unit}" y="{origin + (y + 1) * unit}" '
                              f'width="{unit}" height="{unit}" fill="{INK}"/>')
    for y, row in enumerate(MASCOT):
        for x, cell in enumerate(row):
            if cell != ".":
                pixels.append(f'<rect x="{origin + x * unit}" y="{origin + y * unit}" '
                              f'width="{unit}" height="{unit}" fill="{MASCOT_COLOURS[cell]}"/>')
    parts.append(f'<g shape-rendering="crispEdges">{"".join(pixels)}</g>')
    parts.append(f'<path d="{squircle_path()}" fill="none" stroke="{INK}" stroke-width="32"/>')
    return frame("", "\n".join(parts))


def render(svg, png, size):
    subprocess.run(["resvg", "-w", str(size), "-h", str(size), str(svg), str(png)], check=True)


def write_json(path, images):
    path.write_text(json.dumps({"images": images, "info": {"author": "xcode", "version": 1}}, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preview", type=pathlib.Path, help="scrive solo SVG e PNG a 1024 in questa cartella")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        work = args.preview or pathlib.Path(tmp)
        work.mkdir(parents=True, exist_ok=True)
        modern, retro = work / "modern.svg", work / "retro.svg"
        modern.write_text(modern_svg())
        retro.write_text(retro_svg())
        if args.preview:
            render(modern, work / "modern.png", SIZE)
            render(retro, work / "retro.png", SIZE)
            return

        icon_set = ASSETS / "AppIcon.appiconset"
        for size in (16, 32, 64, 128, 256, 512, 1024):
            render(modern, icon_set / f"app_icon_{size}.png", size)

        image_set = ASSETS / "RetroIcon.imageset"
        image_set.mkdir(exist_ok=True)
        render(retro, image_set / "retro_icon_512.png", 512)
        render(retro, image_set / "retro_icon_1024.png", 1024)
        write_json(image_set / "Contents.json", [
            {"idiom": "mac", "filename": "retro_icon_512.png", "scale": "1x"},
            {"idiom": "mac", "filename": "retro_icon_1024.png", "scale": "2x"},
        ])


if __name__ == "__main__":
    main()
