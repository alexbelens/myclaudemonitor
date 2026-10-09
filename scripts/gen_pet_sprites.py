#!/usr/bin/env python3
"""
Generate src/pet_sprites.h: every sprite of the pet, plus the tables that
say what unlocks when (pet_items) and the achievements (pet_achs).

The art is drawn on a coarse grid (one cell = SCALE device pixels) and baked
pre-scaled into RGB565A8 arrays. LVGL blits uncompressed images straight out
of flash with no RAM copy (lv_bin_decoder.c hands the draw unit a pointer into
the array). Indexed/palette images would look smaller here, but the decoder
expands them to ARGB8888 in RAM on every open, and the 48 KB LVGL heap can't
carry that — so the flash pays instead, which has room to spare.

Coordinates of things Claude wears are in art cells relative to his frame:
col 0 is the frame's left edge, row 0 is the top of his head (frames with
raised arms have rows above the head, so "head" moves per frame).

Run after editing a grid:
    python3 scripts/gen_pet_sprites.py
"""

import os
import sys

OUT = os.path.join(os.path.dirname(__file__), "..", "src", "pet_sprites.h")

PAL = {
    "K": 0x1A1A1A, "T": 0xD77757, "D": 0xB4583D, "L": 0xEC9A7C, "W": 0xFFFFFF, "w": 0xC9D3FF,
    "B": 0x66CCFF, "b": 0x2F6FD0, "Y": 0xFFCC00, "y": 0xC99A00, "R": 0xFF2255, "r": 0xB0103A,
    "O": 0xFF7700, "G": 0x3FBF6A, "g": 0x24804A, "N": 0x8A5A35, "n": 0x5E3B22,
    "S": 0x9999CC, "s": 0x5C5C8A, "H": 0x6E7F3A, "h": 0x4C5A26, "C": 0x00C8FF, "E": 0xEEEEFF,
    "P": 0x8A55E0, "p": 0x5A2FA8, "I": 0xD9DEE8, "i": 0x8C93A8, "m": 0xFF4FD8, "Q": 0xFFE27A,
    # window
    "F": 0x2A2A55, "U": 0x2B6CB0, "V": 0x0B1030, "X": 0x3A4560, "Z": 0x8FA6C8, "M": 0xFFF3B0,
}

SCALE = 4          # Claude and everything he wears
ROOM_SCALE = 3     # room: window, decor, companions — at 4 they don't fit beside him
SMALL_SCALE = 2    # hearts, sparks, star, medals


def grid(rows):
    return [list(r) for r in rows]


def setc(g, r, cols, ch):
    for c in cols:
        g[r][c] = ch


def blank(w, h):
    return [["."] * w for _ in range(h)]


def rows_of(g):
    return ["".join(r) for r in g]


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


def a_frame(eyes="open", arms="down", legs="stand", mouth=False):
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
    elif eyes == "dead":                     # x x — 5H at 100%
        E(2, (6, 9, 12, 15)); E(3, (7, 8, 13, 14)); E(4, (6, 9, 12, 15))
    if mouth:                                # a yawn
        setc(g, 5, (10, 11), "K"); setc(g, 6, (10, 11), "K")

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

    if legs == "walk":
        g[9] = list("......T.T....T.T......")
        g[10] = list("......T.T....T.T......")
    elif legs == "sit":
        g = g[:-2]
    return grid(top) + g, len(top)


def left_hand(g, head):
    """Cell where the left hand is: the end of the arm in cols 2-3 — its top
    when the arm is raised above the head, its bottom otherwise."""
    rows = [r for r in range(len(g)) if g[r][2] != "." or g[r][3] != "."]
    if not rows:
        return 2, head + 6
    return 2, (min(rows) if min(rows) < head else max(rows))


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
    ("yawn",  a_frame(eyes="sleep", mouth=True)),
    ("dead",  a_frame(eyes="dead", legs="sit")),
]


# ── Things Claude wears ──────────────────────────────────────────────────────
CAP = ["...bbbWbbbb.......",
       ".bbbbbbbbbbbb.....",
       "bbbbbbbbbbbbbb....",
       "bbbbbbbbbbFFFFFFFF"]
BEANIE = ["......WW......", ".....CCCC.....", "...CCCCCCCC...", "..CCCCCCCCCC..",
          ".CCCCCCCCCCCC.", "SSSSSSSSSSSSSS"]
PARTY = ["....W....", "....R....", "...RRR...", "...RYR...", "..RRRRR..", "..YRRRY..",
         ".RRRRRRR.", "YYYYYYYYY"]
CROWN = ["Y....Y....Y", "YY..YYY..YY", "YYYYYYYYYYY", "YRYYYBYYYRY", "yyyyyyyyyyy"]
HACHIMAKI = ["WWWWWWWWWRRWWWWWW",
             ".WW..............",
             "W................"]
WIZARD = ["...........PPP..",
          ".........PPPP...",
          ".......PPPPP....",
          "......PPPPPP....",
          ".....PPPYPPP....",
          "....PPPPPPPP....",
          "...PPPPPPPPPP...",
          "..PPYPPPPPPPPP..",
          "pppppppppppppppp"]
HELMET = ["....HHHHHHHH....", "..HHHHHHHHHHHH..", ".HHHHHHHHHHHHHH.", "HHHHHHHHHHHHHHHH",
          "hhhhhhhhhhhhhhhh"]

GLASSES = [".KKKK..KKKK.",
           "KK..KKKK..KK",
           ".K..K..K..K.",
           ".K..K..K..K.",
           ".KKKK..KKKK."]
SUNGLASSES = [".KKKK..KKKK.",
              "KKKKKKKKKKKK",
              ".KwKK..KwKK.",
              ".KKKK..KKKK."]
HEADPHONES = ["...KKKKKKKKKKKK...",
              "..K............K..",
              "..K............K..",
              ".KKK..........KKK.",
              ".KSK..........KSK.",
              ".KKK..........KKK."]
SCARF = ["RRWRRRWRRRWRRR", "RRWRRRWRRRWRRR", "..........RR..", "..........WR..", "..........RR.."]
HOODIE = ["XXXXXWXXWXXXXX",
          "XXXXXWXXWXXXXX",
          "XXXSSSSSSSSXXX",
          "FFFFFFFFFFFFFF"]
VYSHYVANKA = ["WWWWWRKKRWWWWW",
              "WRWWWWRRWWWWRW",
              "RKRWWWRRWWWRKR",
              "RKRKRKRKRKRKRK"]
CLOAK = ["..pppppppppppppp..",            # royal purple: red on terracotta drowned him
         ".pppppppppppppppp.",
         ".pppppppppppppppp.",
         ".pppppppppppppppp.",
         "PppppppppppppppppP",
         "PppppppppppppppppP",
         "PppppppppppppppppP",
         "PppppppppppppppppP",
         "PppppppppppppppppP",
         "YYY.YYYYYYYYYY.YYY"]


def guitar():
    """Slung behind his back: headstock over the right shoulder, body at the
    lower left — the parts that stick out past him are what shows."""
    g = blank(22, 14)
    g[0][19] = g[0][20] = "K"
    for r in range(1, 10):
        c = 19 - r
        g[r][c] = "N"
        if c + 1 < 22:
            g[r][c + 1] = "n"
    for r in range(8, 14):
        for c in range(0, 8):
            if ((r - 10.5) / 2.6) ** 2 + ((c - 3.5) / 3.6) ** 2 <= 1.0:
                g[r][c] = "O"
    g[10][3] = g[10][4] = g[11][3] = g[11][4] = "n"
    return rows_of(g)


WING = ["......WW",
        "....WWWW",
        "..WWWWWW",
        "WWWWWWww",
        "WWWWWWww",
        ".WWWWWww",
        "..WWWwww",
        "...WWw..",
        "....W..."]


def wings():
    g = blank(30, len(WING))
    for r, row in enumerate(WING):
        for c, ch in enumerate(row):
            g[r][c] = ch
        for c, ch in enumerate(row[::-1]):
            g[r][22 + c] = ch
    return rows_of(g)


def aura():
    """Two golden rings around his idle silhouette (one ring was too faint)."""
    base, _ = FRAMES[0][1]
    h, w = len(base), len(base[0])
    pad = 2
    g = blank(w + 2 * pad, h + 2 * pad)

    def solid(r, c):
        r, c = r - pad, c - pad
        return 0 <= r < h and 0 <= c < w and base[r][c] != "."

    for r in range(h + 2 * pad):
        for c in range(w + 2 * pad):
            if solid(r, c):
                continue
            d = min((abs(dr) if abs(dr) > abs(dc) else abs(dc))
                    for dr in range(-2, 3) for dc in range(-2, 3) if solid(r + dr, c + dc)) \
                if any(solid(r + dr, c + dc) for dr in range(-2, 3) for dc in range(-2, 3)) else 9
            if d == 1:
                g[r][c] = "Y"
            elif d == 2:
                g[r][c] = "Q"
    return rows_of(g)


# ── Held in the left hand ────────────────────────────────────────────────────
MUG = ["..w.w..",
       ".w.w...",
       "OOOOO..",
       "OnnnO..",
       "OOOOOOO",
       "OWOOO.O",
       "OOOOOOO",
       ".OOOO.."]


def katana():
    g = blank(12, 12)
    for i in range(8):                       # blade, up and to the left
        g[i][i] = "K"
        g[i][i + 1] = "I"
    g[8][8] = g[8][9] = g[9][8] = "Y"        # guard
    g[9][9] = g[10][10] = "R"                # wrapped grip
    g[11][11] = "K"
    return rows_of(g)


GRIMOIRE = ["pPPPPP",
            "pPYYPP",
            "pYPPYP",
            "pPYYPP",
            "pPPPPP",
            "pWWWWW"]

# ── Companions (two frames each) ─────────────────────────────────────────────
DUCK = [["...YYYY...",
         "..YYYYYY..",
         "..YYKYYOOO",
         "..YYYYYYO.",
         "YYYYYYYY..",
         "YYYyyyYYY.",
         "YYYYYYYYY.",
         ".YYYYYYY..",
         "..OO..OO.."],
        ["...YYYY...",
         "..YYYYYY..",
         "..YYyYYOOO",
         "..YYYYYYO.",
         "YYYYYYYY..",
         "YYYYYyyyY.",
         "YYYYYYYYY.",
         ".YYYYYYY..",
         "..OO..OO.."]]
CRAB = [["OO..........OO",
         "OOO........OOO",
         ".OO..K..K..OO.",
         "..OOOOOOOOOO..",
         "..OOOOOOOOOO..",
         "...OOOOOOOO...",
         "..O.O....O.O..",
         ".O..O....O..O."],
        ["O.O........O.O",
         "OOO........OOO",
         ".OO..K..K..OO.",
         "..OOOOOOOOOO..",
         "..OOOOOOOOOO..",
         "...OOOOOOOO...",
         "...O.O..O.O...",
         "..O..O..O..O.."]]
WHALE = [["....RR.YY.GG....",
          "....RR.YY.GG....",
          "..bbbbbbbbbbbb.b",
          ".bbbbbbbbbbbbbbb",
          "bbbWKbbbbbbbbbb.",
          "bbbbbbbbbbbbbb..",
          "BBBBBBBBBBBbb...",
          ".BBBBBBBBBB.....",
          "..BBBBBBB......."],
         [".B..RR.YY.GG....",
          "B.B.RR.YY.GG....",
          "..bbbbbbbbbbbbb.",
          ".bbbbbbbbbbbbbbb",
          "bbbWKbbbbbbbbbbb",
          "bbbbbbbbbbbbbb..",
          "BBBBBBBBBBBbb...",
          ".BBBBBBBBBB.....",
          "..BBBBBBB......."]]
CAT = [["..S.....S...",
        ".SSS...SSS..",
        ".SSSSSSSSS..",
        ".SKSSSSKSS..",
        ".SSSSmSSSS..",
        "..SSSSSSS...",
        "..SSSSSSS...",
        ".SSSSSSSSS..",
        ".SSSSSSSSSS.",
        ".SSSSSSSSSSS",
        "..SS...SS..S"],
       ["..S.....S...",
        ".SSS...SSS..",
        ".SSSSSSSSS..",
        ".SsSSSSsSS..",
        ".SSSSmSSSS..",
        "..SSSSSSS...",
        "..SSSSSSS..S",
        ".SSSSSSSSS.S",
        ".SSSSSSSSSSS",
        ".SSSSSSSSSS.",
        "..SS...SS..."]]
ROBOT = [["....R.....",
          "....I.....",
          "..IIIIII..",
          "..ICIICI..",
          "..IIIIII..",
          "..IKKKKI..",
          "...IIII...",
          ".IIIIIIII.",
          "IIIFFFFIII",
          ".IIIIIIII.",
          "..II..II..",
          "..ii..ii.."],
         ["....Y.....",
          "....I.....",
          "..IIIIII..",
          "..IIIIII..",
          "..IIIIII..",
          "..IKKKKI..",
          "...IIII...",
          ".IIIIIIII.",
          "IIIFCCFIII",
          ".IIIIIIII.",
          "..II..II..",
          "..ii..ii.."]]

# ── Room ─────────────────────────────────────────────────────────────────────
PLANT = ["....g.......", "...gG..G....", "..gGG.GGg...", ".gGGGgGGGg..", "..gGGGGGg...",
         "...gGGGg....", ".....g......", "...NNNNNN...", "...nNNNNn...", "....NNNN....",
         "....nnnn...."]
LAVA = [["..FFFF..",
         ".FFFFFF.",
         "..PPPP..",
         "..POPP..",
         ".PPPPPP.",
         ".PPPPOP.",
         ".POOPPP.",
         ".PPPPPP.",
         "..PPOP..",
         "..PPPP..",
         ".FFFFFF.",
         "FFFFFFFF",
         ".FFFFFF."],
        ["..FFFF..",
         ".FFFFFF.",
         "..POPP..",
         "..PPPP..",
         ".PPPOPP.",
         ".PPPPPP.",
         ".PPOOPP.",
         ".POPPPP.",
         "..PPPP..",
         "..POPP..",
         ".FFFFFF.",
         "FFFFFFFF",
         ".FFFFFF."]]
SUNFLOWER = ["...Y.Y.Y....",
             "..YYYYYYY...",
             ".YYnnnnnYY..",
             "YYnnnnnnnYY.",
             ".YnnnnnnnY..",
             "YYnnnnnnnYY.",
             ".YYnnnnnYY..",
             "..YYYYYYY...",
             "...Y.G.Y....",
             ".....G.gg...",
             "..gg.G.gg...",
             "..gggG......",
             "...NNNNNN...",
             "...nNNNNn...",
             "....NNNN....",
             "....nnnn...."]


def poster():
    g = blank(17, 11)
    for c in range(17):
        g[0][c] = g[10][c] = "F"
    for r in range(11):
        g[r][0] = g[r][16] = "F"
    for r in range(1, 10):
        for c in range(1, 16):
            g[r][c] = "V"
    for r, c in ((3, 4), (4, 3), (5, 2), (6, 3), (7, 4)):                  # <
        g[r][c] = "C"
    for r, c in ((3, 12), (4, 13), (5, 14), (6, 13), (7, 12)):             # >
        g[r][c] = "C"
    for r, c in ((2, 10), (3, 9), (4, 9), (5, 8), (6, 7), (7, 7), (8, 6)):  # /
        g[r][c] = "O"
    return rows_of(g)


def neon(lit):
    """An eight-point spark in neon; frame 2 flickers one ray off."""
    g = blank(13, 11)
    ch = "O"
    for i in range(11):
        g[i][6] = ch                         # vertical
    for c in range(1, 12):
        g[5][c] = ch                         # horizontal
    for i in range(1, 5):                    # diagonals
        g[5 - i][6 - i] = g[5 - i][6 + i] = g[5 + i][6 - i] = g[5 + i][6 + i] = ch
    g[5][6] = "Y"
    if not lit:
        for i in range(1, 5):
            g[5 + i][6 + i] = "."
    return rows_of(g)


def rack(blink):
    g = blank(11, 16)
    for r in range(16):
        g[r][0] = g[r][10] = "K"
    for c in range(11):
        g[0][c] = g[15][c] = "K"
    leds = ("G", "C", "G", "R", "G", "C") if blink else ("G", "G", "C", "G", "R", "G")
    for u in range(6):
        r = 2 + u * 2
        for c in range(1, 10):
            g[r][c] = "F"
            g[r + 1][c] = "s"
        g[r][8] = leds[u]
    return rows_of(g)


LAPTOP = [["..SSSSSSSSSSSS..", "..SCCCCCCCCCCS..", "..SCWWWCWWCCCS..", "..SCCWWCWWWCCS..",
           "..SCCCCCCCCCCS..", "..SSSSSSSSSSSS..", "SSSSSSSSSSSSSSSS", ".ssssssssssssss."],
          ["..SSSSSSSSSSSS..", "..SCCCCCCCCCCS..", "..SCWWCWWWWCCS..", "..SCWWWWCWCCCS..",
           "..SCCWCCCCCCCS..", "..SSSSSSSSSSSS..", "SSSSSSSSSSSSSSSS", ".ssssssssssssss."]]


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

UMBRELLA = ["........YYYYY........", ".....YYYYYYYYYYY.....", "...YYYYYYYYYYYYYYY...",
            "..YYYYYYYYYYYYYYYYY..", ".yYYYyYYYYyYYYYyYYYy.", "..........n..........",
            "..........n..........", "..........n..........", "..........n.........."]
SWEAT = [".B.", "BBB", "BBB", ".B."]
CLOUD = [["...SSSS...",
          ".SSSSSSSS.",
          "SSSSSSSSSS",
          ".SSSSSSSS.",
          "..B...B...",
          "....B...B."],
         ["...SSSS...",
          ".SSSSSSSS.",
          "SSSSSSSSSS",
          ".SSSSSSSS.",
          "....B...B.",
          "..B...B..."]]
SMOKE = [".SS.", "SSSS", "SsSS", ".SS."]

SMALL = {
    "heart": [".R.R.", "RRRRR", "RRRRR", ".RRR.", "..R.."],
    "spark": ["..Y..", "..Y..", "YYWYY", "..Y..", "..Y.."],
    "star":  ["...Y...", "..YYY..", "YYYYYYY", ".YYYYY.", ".YY.YY.", "Y.....Y"],
}

# ── What unlocks when ────────────────────────────────────────────────────────
# One thing worn/shown per slot: the newest one unlocked (hats can be picked
# by long-press). `until`: level at which it comes off again.
SLOTS = ["HEAD", "FACE", "EARS", "NECK", "SHIRT", "BACK", "HAND", "PET",
         "DECOR", "WALL", "RACK", "AURA"]
ROOM_SLOTS = ("PET", "DECOR", "WALL", "RACK")

# (id, level, slot, name_ru, frames, place, until)
#   wearables: place = (col, head_row)        hand: place = (grip_col, grip_row)
#   room/pet: place = None (positioned in claude_pet.h)
ITEMS = [
    ("cap",         2, "HEAD",  "кепка",               [CAP],            (4, -3), 0),
    ("duck",        3, "PET",   "резиновая уточка",    DUCK,             None, 0),
    ("glasses",     4, "FACE",  "очки",                [GLASSES],        (5, 1), 0),
    ("beanie",      5, "HEAD",  "шапка",               [BEANIE],         (4, -5), 0),
    ("headphones",  6, "EARS",  "наушники",            [HEADPHONES],     (2, -2), 0),
    ("poster",      7, "WALL",  "постер </>",          [poster()],       None, 0),
    ("scarf",       8, "NECK",  "шарф",                [SCARF],          (4, 5), 0),
    ("party",       9, "HEAD",  "колпак",              [PARTY],          (6, -8), 0),
    ("crown",      10, "HEAD",  "корона",              [CROWN],          (5, -5), 0),
    ("crab",       11, "PET",   "краб",                CRAB,             None, 0),
    ("cloak",      12, "BACK",  "плащ",                [CLOAK],          (2, 1), 22),
    ("shades",     13, "FACE",  "тёмные очки",         [SUNGLASSES],     (5, 1), 0),
    ("mug",        14, "HAND",  "кружка кофе",         [MUG],            (6, 5), 0),
    ("hoodie",     15, "SHIRT", "худи",                [HOODIE],         (4, 5), 0),
    ("hachimaki",  16, "HEAD",  "хатимаки",            [HACHIMAKI],      (1, 1), 0),
    ("lava",       17, "DECOR", "лавовая лампа",       LAVA,             None, 0),
    ("whale",      18, "PET",   "кит-контейнеровоз",   WHALE,            None, 0),
    ("neon",       19, "WALL",  "неоновая вывеска",    [neon(True), neon(False)], None, 0),
    ("rack",       20, "RACK",  "серверная стойка",    [rack(False), rack(True)], None, 0),
    ("wizard",     21, "HEAD",  "шляпа волшебника",    [WIZARD],         (3, -8), 0),
    ("katana",     22, "HAND",  "чёрная катана",       [katana()],       (10, 10), 0),
    ("guitar",     23, "BACK",  "гитара",              [guitar()],       (0, -3), 0),
    ("vyshyvanka", 24, "SHIRT", "вышиванка",           [VYSHYVANKA],     (4, 5), 0),
    ("robot",      25, "PET",   "робот",               ROBOT,            None, 0),
    ("cat",        26, "PET",   "кошка",               CAT,              None, 0),
    ("sunflower",  27, "DECOR", "подсолнух",           [SUNFLOWER],      None, 0),
    ("grimoire",   28, "HAND",  "гримуар",             [GRIMOIRE],       (5, 3), 0),
    ("wings",      29, "BACK",  "крылья",              [wings()],        (-4, -1), 0),
    ("aura",       30, "AURA",  "золотая аура",        [aura()],         (-2, -2), 0),
]

# ── Achievements: (id, name_ru, how_ru, ribbon_left, ribbon_right, disc, symbol)
ACHS = [
    ("FIRST",     "Первый ответ",           "Claude закончил первый ответ",          "C", "C", "Y", "W"),
    ("OWL",       "Ночная смена",           "ответ между 2 и 5 ночи",                "P", "P", "S", "M"),
    ("EARLY",     "Ранняя пташка",          "5 дней начаты до 9 утра",               "O", "O", "Y", "O"),
    ("FOREMAN",   "Прораб",                 "три субагента работают одновременно",  "Y", "K", "O", "K"),
    ("FUMES",     "На последнем издыхании", "лимит дошёл до 100%",                   "R", "R", "S", "R"),
    ("MARATHON",  "Марафон",                "100 ответов за день",                   "B", "W", "Y", "B"),
    ("NODAYSOFF", "Без выходных",           "7 дней подряд с Claude",                "G", "G", "Y", "G"),
    ("STEADY",    "Без стоп-крана",         "неделя без упора 5H в 100%",            "C", "W", "W", "C"),
    ("POLYGLOT",  "Полиглот",               "5 разных проектов за день",             "R", "G", "W", "P"),
    ("ARCHIVIST", "Архивариус",             "10 сжатий контекста",                   "N", "N", "Y", "n"),
    ("KIND",      "Добрый хозяин",          "100 раз погладил питомца",              "m", "m", "W", "R"),
    ("WEEKEND",   "Воин выходного дня",     "ответы и в субботу, и в воскресенье",   "b", "Y", "Y", "b"),
    ("THOUSAND",  "Тысячник",               "1000 ответов всего",                    "Y", "Y", "Y", "y"),
    ("JUGGLER",   "Жонглёр",                "5 чатов ждут тебя одновременно",        "O", "P", "C", "O"),
    ("TRUST",     "Доверие",                "100 ответов на разрешения и вопросы",   "G", "W", "G", "W"),
]


def medal(rl, rr, disc, sym):
    return [rl + rl + ".." + rr + rr,
            "." + rl + rl + rr + rr + ".",
            ".." + rl + rr + "..",
            "." + disc * 4 + ".",
            disc * 2 + sym * 2 + disc * 2,
            disc * 2 + sym * 2 + disc * 2,
            disc * 6,
            "." + disc * 4 + "."]


MEDAL_LOCKED = medal("F", "F", "F", "F")


# ── Baking ───────────────────────────────────────────────────────────────────
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


def all_sprites():
    """(c_name, rows, scale) for every image, in emission order."""
    out = []
    for name, (g, _) in FRAMES:
        out.append((name, rows_of(g), SCALE))
    for item_id, _lv, slot, _ru, frames, _place, _until in ITEMS:
        sc = ROOM_SCALE if slot in ROOM_SLOTS else SCALE
        for i, fr in enumerate(frames):
            out.append((f"{item_id}{i}" if len(frames) > 1 else item_id, fr, sc))
    out += [("helmet", HELMET, SCALE), ("umbrella", UMBRELLA, SCALE), ("sweat", SWEAT, SCALE),
            ("plant", PLANT, ROOM_SCALE), ("laptop0", LAPTOP[0], ROOM_SCALE),
            ("laptop1", LAPTOP[1], ROOM_SCALE), ("cloud0", CLOUD[0], ROOM_SCALE),
            ("cloud1", CLOUD[1], ROOM_SCALE), ("smoke", SMOKE, ROOM_SCALE)]
    out += [(n, r, ROOM_SCALE) for n, r in WINDOWS.items()]
    out += [(n, r, SMALL_SCALE) for n, r in SMALL.items()]
    for a in ACHS:
        out.append((f"medal_{a[0].lower()}", medal(*a[3:]), SMALL_SCALE))
    out.append(("medal_locked", MEDAL_LOCKED, SMALL_SCALE))
    return out


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


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    sprites = all_sprites()
    errs = []
    for name, rows, _ in sprites:
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
        f"#define PET_ROOM_SCALE {ROOM_SCALE}",
        "",
    ]
    total = 0
    for name, rows, s in sprites:
        total += emit(out, name, rows, s)

    out.append("/* Pet frames in PF_* order: image, art row where the head starts, and")
    out.append(" * the left-hand cell that held items hang from. */")
    out.append("enum {")
    for name, _ in FRAMES:
        out.append(f"    PF_{name.upper()},")
    out.append("    PF_COUNT")
    out.append("};")
    out.append("typedef struct { const lv_image_dsc_t *img; uint8_t head; int8_t hand_c, hand_r; } pet_frame_t;")
    out.append("static const pet_frame_t pet_frames[PF_COUNT] = {")
    for name, (g, head) in FRAMES:
        hc, hr = left_hand(g, head)
        out.append(f"    {{ &pet_img_{name}, {head}, {hc}, {hr} }},")
    out.append("};")
    out.append("")

    out.append("enum { " + ", ".join(f"SLOT_{s}" for s in SLOTS) + ", SLOT_COUNT };")
    out.append("typedef struct {")
    out.append("    uint8_t level, until, slot;")
    out.append("    int8_t  col, row;      /* wearables: frame col / head row; hand: grip cell */")
    out.append("    const char *name;")
    out.append("    const lv_image_dsc_t *img[2];")
    out.append("} pet_item_t;")
    out.append("static const pet_item_t pet_items[] = {")
    for item_id, lv, slot, ru, frames, place, until in ITEMS:
        col, row = place if place else (0, 0)
        if len(frames) > 1:
            imgs = f"{{ &pet_img_{item_id}0, &pet_img_{item_id}1 }}"
        else:
            imgs = f"{{ &pet_img_{item_id}, NULL }}"
        out.append(f"    {{ {lv}, {until}, SLOT_{slot}, {col}, {row}, {c_str(ru)}, {imgs} }},")
    out.append("};")
    out.append(f"#define PET_ITEM_COUNT {len(ITEMS)}")
    out.append(f"#define PET_MAX_ITEM_LEVEL {max(i[1] for i in ITEMS)}")
    out.append("")

    out.append("enum { " + ", ".join(f"ACH_{a[0]}" for a in ACHS) + ", ACH_COUNT };")
    out.append("typedef struct { const char *name, *how; const lv_image_dsc_t *medal; } pet_ach_t;")
    out.append("static const pet_ach_t pet_achs[ACH_COUNT] = {")
    for a in ACHS:
        out.append(f"    {{ {c_str(a[1])}, {c_str(a[2])}, &pet_img_medal_{a[0].lower()} }},")
    out.append("};")
    out.append("")
    out.append(f"/* Total image data: {total} bytes ({total / 1024:.1f} KB) of flash. */")
    out.append("")
    out.append("#endif /* PET_SPRITES_H */")
    out.append("")

    with open(OUT, "w") as f:
        f.write("\n".join(out))
    print(f"wrote {os.path.normpath(OUT)}: {len(sprites)} images, {total / 1024:.1f} KB of image data")


if __name__ == "__main__":
    main()
