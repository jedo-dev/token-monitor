#!/usr/bin/env python3
"""Иконки дашборда: SVG из макета Claude Design -> бинарники LVGL.

Иконки однотонные, цвет запекается сразу: RGB565A8 (цвет + альфа).
Маскота на месте логотипа рисует отдельный скрипт export_mascot.py.

    python tools/export_icons.py
"""
import io
import math
import os
import struct

import resvg_py
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "main", "icons")

TEXT = "#E6EDF3"
MUTED = "#8B949E"
GREEN = "#3FB950"
ACCENT = "#D97757"
CARD = (0x16, 0x1B, 0x22)

# Облако — ровно тот путь, что в макете (иконка «облачно»).
CLOUD = ("M9.5 25h15a5.5 5.5 0 0 0 .6-10.97A8 8 0 0 0 9.4 15.6 "
         "4.7 4.7 0 0 0 9.5 25z")
# Облако поменьше и повыше — над осадками.
CLOUD_HIGH = ('<g transform="translate(3 -4) scale(0.82)">'
              f'<path d="{CLOUD}"/></g>')


def weather(body: str) -> str:
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" '
            f'viewBox="0 0 32 32" fill="none" stroke="{TEXT}" stroke-width="2" '
            f'stroke-linecap="round" stroke-linejoin="round">{body}</svg>')


def sun_rays(cx, cy, r1, r2, n=8):
    out = []
    for i in range(n):
        a = 2 * math.pi * i / n
        out.append(f"M{cx + r1 * math.cos(a):.2f} {cy + r1 * math.sin(a):.2f}"
                   f"L{cx + r2 * math.cos(a):.2f} {cy + r2 * math.sin(a):.2f}")
    return f'<path d="{"".join(out)}"/>'


WEATHER = {
    "clear":   weather('<circle cx="16" cy="16" r="5.5"/>' + sun_rays(16, 16, 9, 12)),
    "night":   weather('<path d="M22.5 20.5A9 9 0 0 1 11.5 9.5a9 9 0 1 0 11 11z"/>'),
    "partly":  weather('<circle cx="11" cy="11" r="4"/>' + sun_rays(11, 11, 6.5, 8.5)
                       + '<g transform="translate(3 2) scale(0.9)">'
                       f'<path d="{CLOUD}" fill="#161B22"/></g>'),
    "cloudy":  weather(f'<path d="{CLOUD}"/>'),
    "rain":    weather(CLOUD_HIGH + '<path d="M11 24l-1.5 4M16 24l-1.5 4M21 24l-1.5 4"/>'),
    "snow":    weather(CLOUD_HIGH + '<path d="M10.5 25.5h.01M16 27.5h.01M21.5 25.5h.01'
                       'M13 29.5h.01M19 29.5h.01" stroke-width="2.6"/>'),
    "thunder": weather(CLOUD_HIGH + '<path d="M16.5 21.5l-3 4.5h4l-3 4.5"/>'),
    "fog":     weather('<path d="M6 12h20M4 17h24M6 22h20M9 27h14"/>'),
}

SMALL = {
    # восход и закат — из макета без изменений
    "sunrise": ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" '
                f'fill="none" stroke="{MUTED}" stroke-width="1.5" stroke-linecap="round" '
                'stroke-linejoin="round"><path d="M1.5 13.5h13M4.5 13.5a3.5 3.5 0 0 1 7 0M8 2v5"/>'
                '<path d="M5.5 4.5 8 2l2.5 2.5"/></svg>'),
    "sunset":  ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" '
                f'fill="none" stroke="{MUTED}" stroke-width="1.5" stroke-linecap="round" '
                'stroke-linejoin="round"><path d="M1.5 13.5h13M4.5 13.5a3.5 3.5 0 0 1 7 0M8 2v5"/>'
                '<path d="M5.5 4.5 8 7l2.5-2.5"/></svg>'),
    # крестик кнопок модалки: 24 px, линия толще, чтобы легко попасть пальцем
    "close_lg": ('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" '
                 f'fill="none" stroke="{TEXT}" stroke-width="2.5" stroke-linecap="round">'
                 '<path d="M6 6l12 12M18 6L6 18"/></svg>'),
    "check":   ('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" '
                f'fill="none" stroke="{GREEN}" stroke-width="2" stroke-linecap="round" '
                'stroke-linejoin="round"><circle cx="12" cy="12" r="10"/>'
                '<path d="M7.5 12.5l3 3 6-6.5"/></svg>'),
}

def render(svg: str) -> Image.Image:
    return Image.open(io.BytesIO(bytes(resvg_py.svg_to_bytes(svg_string=svg)))).convert("RGBA")


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def to_rgb565a8(img: Image.Image) -> bytes:
    color, alpha = bytearray(), bytearray()
    for r, g, b, a in img.getdata():
        color += struct.pack("<H", rgb565(r, g, b))
        alpha.append(a)
    return bytes(color + alpha)


def to_rgb565(img: Image.Image) -> bytes:
    out = bytearray()
    for r, g, b, *_ in img.convert("RGB").getdata():
        out += struct.pack("<H", rgb565(r, g, b))
    return bytes(out)


def main():
    os.makedirs(OUT, exist_ok=True)

    # все погодные иконки одним файлом, порядок как в enum на плате
    blob = b"".join(to_rgb565a8(render(WEATHER[k])) for k in WEATHER)
    open(os.path.join(OUT, "weather.bin"), "wb").write(blob)
    print(f"weather.bin: {len(WEATHER)} иконок 32x32 ({', '.join(WEATHER)}), {len(blob)} байт")

    for name, svg in SMALL.items():
        data = to_rgb565a8(render(svg))
        open(os.path.join(OUT, f"{name}.bin"), "wb").write(data)
        print(f"{name}.bin: {len(data)} байт")

    # превью для глаз
    sheet = Image.new("RGBA", (len(WEATHER) * 40 + 8, 48), CARD + (255,))
    for i, k in enumerate(WEATHER):
        sheet.alpha_composite(render(WEATHER[k]), (8 + i * 40, 8))
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(
        os.path.join(HERE, "preview", "weather_icons.png"))


if __name__ == "__main__":
    main()
