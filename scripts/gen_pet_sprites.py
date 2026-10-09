#!/usr/bin/env python3
"""
Generate src/pet_sprites.h from the pixel grids in this file.

The art is drawn on a coarse grid (one cell = SCALE device pixels) and baked
pre-scaled into RGB565A8 arrays. LVGL blits uncompressed images straight out
of flash with no RAM copy (lv_bin_decoder.c hands the draw unit a pointer into
the array). Indexed/palette images would look smaller here, but the decoder
expands them to ARGB8888 in RAM on every open, and the 48 KB LVGL heap can't
carry that — so the flash pays instead, which has room to spare.

The grids are the same ones the browser prototype was drawn with, so what was
approved there is what lands on the screen.

Run after editing a grid:
    python3 scripts/gen_pet_sprites.py
"""

import os
import sys

OUT = os.path.join(os.path.dirname(__file__), "..", "src", "pet_sprites.h")

PAL = {
    "K": 0x1A1A1A, "T": 0xD77757, "D": 0xB4583D, "L": 0xEC9A7C, "W": 0xFFFFFF, "w": 0xC9D3FF,
    "B": 0x66CCFF, "Y": 0xFFCC00, "y": 0xC99A00, "R": 0xFF2255, "O": 0xFF7700,
    "G": 0x3FBF6A, "g": 0x24804A, "N": 0x8A5A35, "n": 0x5E3B22, "S": 0x9999CC, "s": 0x5C5C8A,
    "H": 0x6E7F3A, "h": 0x4C5A26, "C": 0x00C8FF, "E": 0xEEEEFF,
    # window
    "F": 0x2A2A55, "U": 0x2B6CB0, "V": 0x0B1030, "X": 0x3A4560, "Z": 0x8FA6C8, "M": 0xFFF3B0,
}


def grid(rows):
    return [list(r) for r in rows]


def setc(g, r, cols, ch):
    for c in cols:
        g[r][c] = ch


# ── Claude Code: the CLI banner creature, grown from 18x5 to 22x11 ──────────
A_BASE = [
    "....TTTTTTTTTTTTTT....",
    "....TLLLLLLLLLLLLT....",
    "....TTTKKTTTTKKTTT....",
    "....TTTKKTTTTKKTTT....",
    "....TTTKKTTTTKKTTT....",
    "..TTTTTTTTTTTTTTTTTT..",
    "..TTTTTTTTTTTTTTTTTT..",
    "....TTTTTTTTTTTTTT....",
    "....DDDDDDDDDDDDDD....",
    ".....T.T......T.T.....",
    ".....T.T......T.T.....",
]


def a_frame(eyes="open", arms="down", legs="stand"):
    """Returns (grid, head_row): head_row is where the head starts, which
    moves down when raised arms add rows above it."""
    g = grid(A_BASE)
    for r in (2, 3, 4):
        setc(g, r, (6, 7, 8, 9, 12, 13, 14, 15), "T")

    def E(r, cols):
        setc(g, r, cols, "K")

    if eyes == "open":
        for r in (2, 3, 4):
            E(r, (7, 8, 13, 14))
    elif eyes == "blink":
        E(4, (7, 8, 13, 14))
    elif eyes == "happy":
        E(2, (7, 8, 13, 14)); E(3, (6, 9, 12, 15))
    elif eyes == "sleep":
        E(3, (6, 9, 12, 15)); E(4, (7, 8, 13, 14))
    elif eyes == "tired":
        E(3, (7, 8, 13, 14)); E(4, (6, 15))
    elif eyes == "right":
        for r in (2, 3, 4):
            E(r, (8, 9, 14, 15))

    top = []
    if arms == "up":
        setc(g, 5, (2, 3, 18, 19), "."); setc(g, 6, (2, 3, 18, 19), "."); setc(g, 0, (2, 3, 18, 19), "T")
        top = ["..TT..............TT..", "..TT..............TT.."]
    elif arms in ("wave0", "hold"):
        setc(g, 5, (18, 19), "."); setc(g, 6, (18, 19), "."); setc(g, 0, (18, 19), "T")
        top = ["..................TT..", "..................TT.."]
    elif arms == "wave1":
        setc(g, 5, (18, 19), "."); setc(g, 6, (18, 19), "."); setc(g, 0, (18, 19), "T")
        top = ["...................TT.", "..................TT.."]
    elif arms == "typeA":
        setc(g, 5, (18, 19), "."); setc(g, 6, (2, 3), ".")
    elif arms == "typeB":
        setc(g, 5, (2, 3), "."); setc(g, 6, (18, 19), ".")

    if legs == "sit":
        g = g[:-2]
    return grid(top) + g, len(top)


# Order here = PF_* enum order in C.
FRAMES = [
    ("idle",  a_frame()),
    ("blink", a_frame(eyes="blink")),
    ("work0", a_frame(eyes="right", arms="typeA")),
    ("work1", a_frame(eyes="right", arms="typeB")),
    ("wave0", a_frame(arms="wave0")),
    ("wave1", a_frame(arms="wave1")),
    ("tired", a_frame(eyes="tired")),
    ("sleep", a_frame(eyes="sleep", legs="sit")),
    ("joy",   a_frame(eyes="happy", arms="up")),
    ("hold",  a_frame(arms="hold")),
    ("bored", a_frame(eyes="tired", legs="sit")),
]

HATS = {
    "beanie": ["......WW......", ".....CCCC.....", "...CCCCCCCC...", "..CCCCCCCCCC..",
               ".CCCCCCCCCCCC.", "SSSSSSSSSSSSSS"],
    "party":  ["....W....", "....R....", "...RRR...", "...RYR...", "..RRRRR..", "..YRRRY..",
               ".RRRRRRR.", "YYYYYYYYY"],
    "crown":  ["Y....Y....Y", "YY..YYY..YY", "YYYYYYYYYYY", "YRYYYBYYYRY", "yyyyyyyyyyy"],
    "helmet": ["....HHHHHHHH....", "..HHHHHHHHHHHH..", ".HHHHHHHHHHHHHH.", "HHHHHHHHHHHHHHHH",
               "hhhhhhhhhhhhhhhh"],
}

OVERLAYS = {
    "umbrella": [
        "........YYYYY........", ".....YYYYYYYYYYY.....", "...YYYYYYYYYYYYYYY...",
        "..YYYYYYYYYYYYYYYYY..", ".yYYYyYYYYyYYYYyYYYy.", "..........n..........",
        "..........n..........", "..........n..........", "..........n..........",
    ],
    "scarf": ["RRWRRRWRRRWRRR", "RRWRRRWRRRWRRR", "..........RR..", "..........WR..", "..........RR.."],
    "sweat": [".B.", "BBB", "BBB", ".B."],
}

ITEMS = {
    "plant": ["....g.......", "...gG..G....", "..gGG.GGg...", ".gGGGgGGGg..", "..gGGGGGg...",
              "...gGGGg....", ".....g......", "...NNNNNN...", "...nNNNNn...", "....NNNN....",
              "....nnnn...."],
    "mug": ["..w..w...", "...w..w..", "OWWWWWO..", "OOOOOOOOO", "OOOOOOO.O", "OOOOOOO.O",
            "OOOOOOOOO", "OOOOOOO..", ".OOOOO..."],
    "trophy": [".YYYYYYYY.", "YYYYYYYYYY", "Y.YYYYYY.Y", "Y.YYWYYY.Y", ".YYYYYYYY.", "..YYYYYY..",
               "...YYYY...", "....YY....", "....YY....", "..yyyyyy..", "..YYYYYY.."],
    "laptop0": ["..SSSSSSSSSSSS..", "..SCCCCCCCCCCS..", "..SCWWWCWWCCCS..", "..SCCWWCWWWCCS..",
                "..SCCCCCCCCCCS..", "..SSSSSSSSSSSS..", "SSSSSSSSSSSSSSSS", ".ssssssssssssss."],
    "laptop1": ["..SSSSSSSSSSSS..", "..SCCCCCCCCCCS..", "..SCWWCWWWWCCS..", "..SCWWWWCWCCCS..",
                "..SCCWCCCCCCCS..", "..SSSSSSSSSSSS..", "SSSSSSSSSSSSSSSS", ".ssssssssssssss."],
}


def window(sky, deco=()):
    """15x11 window: frame + cross bars, sky fill, then (row, col, colour) dots."""
    w, h = 15, 11
    g = [[sky] * w for _ in range(h)]
    for c in range(w):
        g[0][c] = g[h - 1][c] = g[5][c] = "F"
    for r in range(h):
        g[r][0] = g[r][w - 1] = g[r][7] = "F"
    for r, c, ch in deco:
        if g[r][c] != "F":
            g[r][c] = ch
    return ["".join(row) for row in g]


WINDOWS = {
    "win_day":   window("U", [(3, 2, "W"), (3, 3, "W"), (3, 4, "W"), (2, 3, "W"), (3, 5, "w"),
                              (7, 10, "W"), (7, 11, "W"), (6, 11, "W"), (7, 12, "w")]),
    "win_night": window("V", [(2, 10, "M"), (3, 10, "M"), (2, 11, "M"), (4, 11, "M"), (3, 9, "M"),
                              (2, 3, "w"), (4, 5, "w"), (8, 2, "w"), (7, 12, "w")]),
    "win_rain0": window("X", [(1, 2, "B"), (2, 2, "B"), (3, 5, "B"), (4, 5, "B"), (1, 10, "B"),
                              (2, 10, "B"), (6, 3, "B"), (7, 3, "B"), (8, 9, "B"), (9, 9, "B"),
                              (6, 12, "B"), (7, 12, "B")]),
    "win_rain1": window("X", [(3, 2, "B"), (4, 2, "B"), (1, 5, "B"), (2, 5, "B"), (3, 10, "B"),
                              (4, 10, "B"), (8, 3, "B"), (9, 3, "B"), (6, 9, "B"), (7, 9, "B"),
                              (8, 12, "B"), (9, 12, "B")]),
    "win_snow0": window("Z", [(1, 2, "W"), (3, 5, "W"), (2, 9, "W"), (4, 12, "W"), (6, 3, "W"),
                              (8, 5, "W"), (7, 10, "W"), (9, 12, "W")]),
    "win_snow1": window("Z", [(2, 3, "W"), (4, 4, "W"), (3, 10, "W"), (1, 12, "W"), (7, 2, "W"),
                              (9, 6, "W"), (8, 9, "W"), (6, 13, "W")]),
}

SMALL = {  # drawn at scale 2
    "heart": [".R.R.", "RRRRR", "RRRRR", ".RRR.", "..R.."],
    "spark": ["..Y..", "..Y..", "YYWYY", "..Y..", "..Y.."],
    "star":  ["...Y...", "..YYY..", "YYYYYYY", ".YYYYY.", ".YY.YY.", "Y.....Y"],
    "tail":  ["EEEE", "EEE.", "EE..", "E..."],
}

SCALE = 4          # Claude and everything he wears
ROOM_SCALE = 3     # window and room items: at 4 they don't fit beside him
SMALL_SCALE = 2    # hearts, sparks, star, bubble tail


def validate(name, rows):
    w = len(rows[0])
    errs = []
    for i, r in enumerate(rows):
        if len(r) != w:
            errs.append(f"{name}: row {i} is {len(r)} wide, expected {w}")
        for ch in r:
            if ch != "." and ch not in PAL:
                errs.append(f"{name}: unknown colour '{ch}'")
    return errs


def rgb565(c):
    r, g, b = (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def bake(rows, s):
    """RGB565 plane (little-endian, stride w*2) followed by the A8 plane
    (stride w) — the layout lv_draw_sw_img.c expects for RGB565A8."""
    aw, ah = len(rows[0]), len(rows)
    w, h = aw * s, ah * s
    color = bytearray()
    alpha = bytearray()
    for y in range(h):
        row = rows[y // s]
        for x in range(w):
            ch = row[x // s]
            if ch == ".":
                color += b"\x00\x00"
                alpha.append(0)
            else:
                v = rgb565(PAL[ch])
                color += bytes((v & 0xFF, v >> 8))
                alpha.append(0xFF)
    return w, h, bytes(color + alpha)


def emit(out, name, rows, s):
    w, h, data = bake(rows, s)
    out.append(f"/* {name}: {len(rows[0])}x{len(rows)} art px, {w}x{h} on screen */")
    out.append(f"static const uint8_t pet_map_{name}[] = {{")
    for i in range(0, len(data), 24):
        out.append("    " + ",".join(f"0x{b:02X}" for b in data[i:i + 24]) + ",")
    out.append("};")
    # Positional init: nested designators (.header.cf) don't compile as C++,
    # and main.cpp includes this. Field order is lv_image_header_t's
    # little-endian layout: magic, cf, flags, w, h, stride, reserved_2.
    out.append(f"static const lv_image_dsc_t pet_img_{name} = {{")
    out.append(f"    {{ LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_RGB565A8, 0, {w}, {h}, {w * 2}, 0 }},")
    out.append(f"    {len(data)}, pet_map_{name}, NULL")
    out.append("};")
    out.append("")
    return len(data)


def main():
    errs = []
    for name, (g, _) in FRAMES:
        errs += validate(name, ["".join(r) for r in g])
    for table in (HATS, OVERLAYS, ITEMS, WINDOWS, SMALL):
        for name, rows in table.items():
            errs += validate(name, rows)
    if errs:
        print("\n".join(errs), file=sys.stderr)
        sys.exit(1)

    out = [
        "/* GENERATED by scripts/gen_pet_sprites.py — edit the grids there, not this file. */",
        "#ifndef PET_SPRITES_H",
        "#define PET_SPRITES_H",
        "",
        "#include <stdint.h>",
        "#include \"lvgl.h\"",
        "",
        f"#define PET_SCALE {SCALE}",
        "",
    ]
    total = 0
    for name, (g, _) in FRAMES:
        total += emit(out, name, ["".join(r) for r in g], SCALE)
    for table in (HATS, OVERLAYS):
        for name, rows in table.items():
            total += emit(out, name, rows, SCALE)
    for table in (ITEMS, WINDOWS):
        for name, rows in table.items():
            total += emit(out, name, rows, ROOM_SCALE)
    for name, rows in SMALL.items():
        total += emit(out, name, rows, SMALL_SCALE)

    out.append("/* Pet frames in PF_* order, with the art row where the head starts. */")
    out.append("enum {")
    for name, _ in FRAMES:
        out.append(f"    PF_{name.upper()},")
    out.append("    PF_COUNT")
    out.append("};")
    out.append("typedef struct { const lv_image_dsc_t *img; uint8_t head; } pet_frame_t;")
    out.append("static const pet_frame_t pet_frames[PF_COUNT] = {")
    for name, (_, head) in FRAMES:
        out.append(f"    {{ &pet_img_{name}, {head} }},")
    out.append("};")
    out.append("")
    out.append(f"/* Total image data: {total} bytes ({total / 1024:.1f} KB) of flash. */")
    out.append("")
    out.append("#endif /* PET_SPRITES_H */")
    out.append("")

    with open(OUT, "w") as f:
        f.write("\n".join(out))
    print(f"wrote {os.path.normpath(OUT)}: {total / 1024:.1f} KB of image data")


if __name__ == "__main__":
    main()
