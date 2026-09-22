#!/usr/bin/env python3
"""Маскот Claude Code: пиксельная анимация вместо логотипа.

Маскот из официального SVG (сетка 24x24 с шагом 1.5) переведён в клетки
по 0.5 единицы SVG, так что все его грани целые. Кадр — 64x64 клетки,
клетка — 2x2 пикселя экрана, итого 128x128 RGB565 на фоне карточки.

Три состояния и переходы между ними:
    idle   — стоит, дышит, моргает, оглядывается, подпрыгивает;
    work   — сидит за ноутбуком и печатает (пока Claude работает);
    sleep  — ночной колпак, закрытые глаза, «z Z Z».

Скрипт пишет main/icons/mascot.bin (все уникальные кадры подряд),
main/mascot_anim.h (последовательности кадров с длительностями)
и GIF-превью в tools/preview/.

    python tools/export_mascot.py
"""
import os
import struct

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ICONS = os.path.join(ROOT, "main", "icons")
PREVIEW = os.path.join(HERE, "preview")

GRID = 64                # клеток в кадре
CELL = 2                 # пикселей в клетке
SIZE = GRID * CELL       # 128

BG = (0x16, 0x1B, 0x22)      # фон карточки
BODY = (0xD9, 0x77, 0x57)    # терракотовый маскот
LID = (0x6E, 0x76, 0x81)     # крышка ноутбука
LID_HI = (0x8B, 0x94, 0x9E)
DECK = (0x8B, 0x94, 0x9E)    # клавиатурная часть
DECK_LO = (0x48, 0x4F, 0x58)
GLOW = (0xE6, 0xED, 0xF3)
CAP = (0x58, 0x7B, 0xD8)     # ночной колпак
CAP_LO = (0x44, 0x60, 0xB0)
WHITE = (0xE6, 0xED, 0xF3)
MUTED = (0x8B, 0x94, 0x9E)

# Маскот в клетках: x 0..48, y 10..40 (координаты SVG * 2).
OX, OY = 8, 12           # где он стоит в кадре: ноги на y = 52


class Pose:
    def __init__(self, **kw):
        self.dy = 0          # присел: тело ниже, ноги короче
        self.lift = 0        # подпрыгнул целиком
        self.eyes = "open"   # open closed half down up left right wide
        self.arm_l = 0       # сдвиг рук по вертикали, + вниз
        self.arm_r = 0
        self.legs = None     # "l" / "r" — поджал пару ног
        self.laptop = None   # None или сдвиг ноутбука вниз (0 — на месте)
        self.cap = None      # None или сдвиг колпака вверх (0 — надет)
        self.zzz = 0         # сколько «z» над головой
        self.spark = None    # искорка: (x, y, размер)
        self.bang = False    # «!» над головой
        self.__dict__.update(kw)


def rect(d, x0, y0, x1, y1, color):
    """Прямоугольник в клетках, правая и нижняя грани не включаются."""
    if x1 > x0 and y1 > y0:
        d.rectangle([x0, y0, x1 - 1, y1 - 1], fill=color)


EYES = {
    "open":   (0, 16, 0, 22),
    "closed": (0, 20, 0, 22),
    "half":   (0, 19, 0, 22),
    "down":   (0, 18, 0, 24),
    "up":     (0, 14, 0, 20),
    "left":   (-2, 16, -2, 22),
    "right":  (2, 16, 2, 22),
    "wide":   (0, 14, 0, 22),
}

def glyph_z(n):
    mid = ["." * (n - 2 - k) + "#" + "." * (k + 1) for k in range(n - 2)]
    return ["#" * n] + mid + ["#" * n]


GLYPH_Z = {n: glyph_z(n) for n in (5, 6, 8)}
GLYPH_BANG = ["##", "##", "##", "##", "..", "##"]


def glyph(d, rows, x, y, color):
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch == "#":
                rect(d, x + i, y + j, x + i + 1, y + j + 1, color)


def draw_cap(d, x, y):
    """Колпак, свисающий влево; (x, y) — левый верх ободка в клетках кадра."""
    # конус с тенью справа
    d.polygon([(x + 1, y - 1), (x + 37, y - 1), (x + 22, y - 12), (x + 16, y - 12)], fill=CAP)
    d.polygon([(x + 27, y - 1), (x + 37, y - 1), (x + 22, y - 12), (x + 21, y - 12)],
              fill=CAP_LO)
    # кончик перегибается и свисает влево
    d.line([(x + 19, y - 11), (x + 15, y - 15), (x + 9, y - 15), (x + 5, y - 11)],
           fill=CAP, width=5, joint="curve")
    d.polygon([(x + 9, y - 12), (x + 17, y - 12), (x + 15, y - 15)], fill=CAP)
    # помпон
    d.ellipse([x + 1, y - 11, x + 7, y - 5], fill=WHITE)
    # ободок
    rect(d, x - 1, y, x + 39, y + 3, WHITE)


def draw_laptop(d, off):
    """Ноутбук перед маскотом, к нам задней стороной крышки."""
    top = OY + 25 + off
    rect(d, OX + 8, top, OX + 40, top + 14, LID)
    rect(d, OX + 8, top, OX + 40, top + 1, LID_HI)
    rect(d, OX + 22, top + 5, OX + 26, top + 9, GLOW)        # светящийся логотип
    rect(d, OX + 3, top + 14, OX + 45, top + 17, DECK)
    rect(d, OX + 3, top + 16, OX + 45, top + 17, DECK_LO)


def draw_spark(d, x, y, r):
    rect(d, x - r, y, x + r + 1, y + 1, WHITE)
    rect(d, x, y - r, x + 1, y + r + 1, WHITE)


def render(p: Pose) -> Image.Image:
    img = Image.new("RGB", (GRID, GRID), BG)
    d = ImageDraw.Draw(img)
    ox, oy = OX, OY - p.lift
    b = p.dy                                  # сдвиг тела вниз

    # ноги от тела до пола
    for x0, x1, side in ((9, 12, "l"), (15, 18, "l"), (30, 33, "r"), (36, 39, "r")):
        bottom = 40 - (1 if p.legs == side else 0)
        rect(d, ox + x0, oy + 34 + b, ox + x1, oy + bottom, BODY)
    # тело и руки
    rect(d, ox + 6, oy + 10 + b, ox + 42, oy + 34 + b, BODY)
    rect(d, ox + 0, oy + 22 + b + p.arm_l, ox + 6, oy + 28 + b + p.arm_l, BODY)
    rect(d, ox + 42, oy + 22 + b + p.arm_r, ox + 48, oy + 28 + b + p.arm_r, BODY)
    # глаза — «дырки» в теле
    ex, ey0, _, ey1 = EYES[p.eyes]
    for x0 in (12, 33):
        rect(d, ox + x0 + ex, oy + ey0 + b, ox + x0 + 3 + ex, oy + ey1 + b, BG)

    if p.cap is not None:
        draw_cap(d, ox + 5, oy + 7 + b - p.cap)
    if p.laptop is not None:
        draw_laptop(d, p.laptop)
    if p.spark:
        draw_spark(d, *p.spark)
    if p.bang:
        glyph(d, GLYPH_BANG, 55, 6, WHITE)
    zs = [(5, 53, 16, MUTED), (6, 57, 8, MUTED), (8, 53, 0, WHITE)]
    for size, x, y, color in zs[:p.zzz]:
        glyph(d, GLYPH_Z[size], x, y, color)
    return img


# ---------- кадры ----------

FRAMES = {}


def frame(name, **kw):
    FRAMES[name] = Pose(**kw)
    return name


# стоит
frame("base")
frame("crouch", dy=1)
frame("blink", eyes="closed")
frame("look_l", eyes="left")
frame("look_r", eyes="right")
frame("hop", lift=2, arm_l=-2, arm_r=-2)
frame("leg_l", legs="l", arm_l=-1)
frame("leg_r", legs="r", arm_r=-1)
# берёт ноутбук
for i, off in enumerate((12, 6, 2)):
    frame(f"lap{i}", eyes="down", laptop=off)
# печатает
frame("type_a", eyes="down", laptop=0, arm_l=2, arm_r=0)
frame("type_b", eyes="down", laptop=0, arm_l=0, arm_r=2, dy=1)
frame("type_a_s", eyes="down", laptop=0, arm_l=2, arm_r=0, spark=(10, 16, 1))
frame("type_b_s", eyes="down", laptop=0, arm_l=0, arm_r=2, dy=1, spark=(54, 12, 2))
frame("think", eyes="up", laptop=0, arm_l=0, arm_r=0)
frame("type_blink", eyes="closed", laptop=0, arm_l=1, arm_r=1)
# засыпает
frame("drowsy", eyes="half", dy=1)
frame("doze", eyes="closed", dy=1)
frame("cap_hi", eyes="closed", dy=1, cap=10)
frame("cap_mid", eyes="closed", dy=1, cap=4)
# спит
frame("sleep0", eyes="closed", cap=0, zzz=1)
frame("sleep1", eyes="closed", cap=0, zzz=2)
frame("sleep2", eyes="closed", cap=0, dy=1, zzz=3)
frame("sleep3", eyes="closed", cap=0, dy=1, zzz=0)
# просыпается
frame("wake0", eyes="wide", cap=6, bang=True, arm_l=-3, arm_r=-3)
frame("wake1", eyes="wide", bang=True, lift=2, arm_l=-3, arm_r=-3)

# ---------- последовательности (кадр, мс) ----------

SEQS = {
    "idle": [
        ("base", 450), ("crouch", 450), ("base", 450), ("crouch", 450),
        ("blink", 140), ("base", 450), ("crouch", 450), ("base", 300),
        ("look_l", 900), ("base", 250), ("look_r", 900), ("base", 450),
        ("crouch", 450), ("base", 450), ("crouch", 200), ("hop", 180),
        ("base", 200), ("hop", 180), ("base", 450), ("leg_l", 250),
        ("leg_r", 250), ("leg_l", 250), ("leg_r", 250), ("base", 450),
        ("blink", 140), ("base", 450), ("crouch", 450),
    ],
    "laptop_in": [("lap0", 110), ("lap1", 110), ("lap2", 110)],
    "work": [
        ("type_a", 150), ("type_b", 150), ("type_a", 150), ("type_b", 150),
        ("type_a_s", 150), ("type_b", 150), ("type_a", 150), ("type_b_s", 150),
        ("type_a", 150), ("type_b", 150), ("type_a", 150), ("type_b", 150),
        ("think", 700), ("type_blink", 120), ("type_a", 150), ("type_b", 150),
        ("type_a", 150), ("type_b_s", 150), ("type_a_s", 150), ("type_b", 150),
    ],
    "laptop_out": [("lap2", 110), ("lap1", 110), ("lap0", 110)],
    "fall_asleep": [
        ("drowsy", 600), ("base", 300), ("drowsy", 700), ("doze", 600),
        ("cap_hi", 120), ("cap_mid", 120), ("sleep3", 500),
    ],
    "sleep": [("sleep0", 800), ("sleep1", 800), ("sleep2", 800), ("sleep3", 800)],
    "wake": [("wake0", 180), ("wake1", 350), ("base", 150)],
}


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def to_rgb565(img):
    out = bytearray()
    raw = img.tobytes()
    for i in range(0, len(raw), 3):
        r, g, b = raw[i:i + 3]
        out += struct.pack("<H", rgb565(r, g, b))
    return bytes(out)


def full(img):
    return img.resize((SIZE, SIZE), Image.NEAREST)


def main():
    names = list(FRAMES)
    index = {n: i for i, n in enumerate(names)}
    images = {n: full(render(FRAMES[n])) for n in names}

    blob = b"".join(to_rgb565(images[n]) for n in names)
    open(os.path.join(ICONS, "mascot.bin"), "wb").write(blob)
    print(f"mascot.bin: {len(names)} кадров {SIZE}x{SIZE}, {len(blob)} байт")

    lines = [
        "/* Сгенерировано tools/export_mascot.py — не править руками. */",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"#define MASCOT_SIZE   {SIZE}",
        f"#define MASCOT_FRAMES {len(names)}",
        "",
        "typedef struct { uint8_t frame; uint16_t ms; } mascot_step_t;",
        "",
    ]
    for seq, steps in SEQS.items():
        body = ", ".join(f"{{{index[f]}, {ms}}}" for f, ms in steps)
        lines.append(f"static const mascot_step_t MASCOT_{seq.upper()}[] = {{{body}}};")
    open(os.path.join(ROOT, "main", "mascot_anim.h"), "w", encoding="utf-8").write(
        "\n".join(lines) + "\n")

    # превью: каждое состояние отдельным GIF, крупно
    os.makedirs(PREVIEW, exist_ok=True)
    show = {
        "idle": ["idle"],
        "work": ["laptop_in", "work", "work", "laptop_out"],
        "sleep": ["fall_asleep", "sleep", "sleep", "wake"],
    }
    for name, seqs in show.items():
        steps = [s for q in seqs for s in SEQS[q]]
        frames = [images[f].resize((SIZE * 2, SIZE * 2), Image.NEAREST) for f, _ in steps]
        frames[0].save(os.path.join(PREVIEW, f"mascot_{name}.gif"), save_all=True,
                       append_images=frames[1:], duration=[ms for _, ms in steps], loop=0)
    sheet = Image.new("RGB", (len(names) * (SIZE + 8) + 8, SIZE + 16), (0x0D, 0x11, 0x17))
    for i, n in enumerate(names):
        sheet.paste(images[n], (8 + i * (SIZE + 8), 8))
    sheet.save(os.path.join(PREVIEW, "mascot_sheet.png"))


if __name__ == "__main__":
    main()
