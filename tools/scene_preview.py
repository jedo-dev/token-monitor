#!/usr/bin/env python3
"""Сборка пиксельной сцены Token Monitor 480x480 из ассетов LimeZu.

Рисует ровно то, что потом увидит плата: тот же размер, те же спрайты,
без «дизайнерского» промежуточного макета. Запуск:

    python tools/scene_preview.py            # один кадр
    python tools/scene_preview.py --gif      # анимация, чтобы оценить движение
"""
import os
import sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PRESETS = os.path.join(ROOT, "presets")
OUT = os.path.join(HERE, "preview")

SCR = 480
TILE = 16

OFFICE = os.path.join(PRESETS, "Modern_Office_Revamped_v1.2", "6_Office_Designs",
                      "Office_Design_1.gif")
CHARS = os.path.join(PRESETS, "Modern-interiors", "2_Characters",
                     "Character_Generator", "0_Premade_Characters", "16x16")

# Палитра интерфейса — та же, что в прошивке
COL_BG = (13, 17, 23)
COL_TEXT = (230, 237, 243)
COL_MUTED = (139, 148, 158)
COL_ACCENT = (217, 119, 87)
COL_GREEN = (63, 185, 80)


def char_frame(index: int, col: int, row: int) -> Image.Image:
    """Кадр из листа персонажа: лист 896x656 нарезан по 16x16."""
    path = os.path.join(CHARS, f"Premade_Character_{index:02d}.png")
    sheet = Image.open(path).convert("RGBA")
    box = (col * TILE, row * TILE, (col + 1) * TILE, (row + 1) * TILE)
    return sheet.crop(box)


def build_frame(frame_idx: int, busy: bool = True) -> Image.Image:
    scene = Image.new("RGBA", (SCR, SCR), COL_BG + (255,))

    office = Image.open(OFFICE)
    office.seek(frame_idx % office.n_frames)
    room = office.convert("RGBA")

    # комната по центру, сверху оставляем полосу под данные
    ox = (SCR - room.width) // 2
    oy = 44
    scene.alpha_composite(room, (ox, oy))

    # человечек за рабочим местом; кадр меняется — получается «печатает»
    hero = char_frame(1, (frame_idx % 2) * 6, 0)
    hero = hero.resize((TILE * 2, TILE * 2), Image.NEAREST)
    scene.alpha_composite(hero, (ox + 96, oy + 120))

    draw = ImageDraw.Draw(scene)

    # верхняя строка данных
    draw.text((12, 8), "TOKEN MONITOR", fill=COL_ACCENT)
    draw.text((SCR - 96, 8), "10:21", fill=COL_TEXT)
    draw.text((12, 24), "Session 30%   Week 41%", fill=COL_MUTED)

    # нижняя строка состояния
    status = "Claude печатает..." if busy else "Простой"
    draw.text((12, SCR - 22), status, fill=COL_GREEN if busy else COL_MUTED)
    draw.text((SCR - 150, SCR - 22), "Почта: 11   146 R", fill=COL_MUTED)

    return scene.convert("RGB")


def main():
    os.makedirs(OUT, exist_ok=True)
    if "--gif" in sys.argv:
        frames = [build_frame(i) for i in range(6)]
        path = os.path.join(OUT, "scene.gif")
        frames[0].save(path, save_all=True, append_images=frames[1:],
                       duration=180, loop=0)
        print("готово:", path)
    else:
        path = os.path.join(OUT, "scene.png")
        build_frame(0).save(path)
        print("готово:", path, "— откройте, чтобы оценить")


if __name__ == "__main__":
    main()
