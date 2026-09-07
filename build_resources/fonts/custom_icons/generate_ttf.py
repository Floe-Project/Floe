# Copyright 2026 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generates custom_icons.ttf from the SVGs in this directory. Run from the repo root:
#   nix shell nixpkgs#fontforge --command fontforge -lang=py -script build_resources/fonts/custom_icons/generate_ttf.py
#
# Codepoints must stay in the Private Use Area below ICON_MIN_FA (U+E005) so they never collide with Font
# Awesome. Keep in sync with custom_icons.hpp.

import os

import fontforge

icons = {
    "midi": 0xE000,
}

here = os.path.dirname(os.path.abspath(__file__))

font = fontforge.font()
font.familyname = "Floe Custom Icons"
font.fontname = "FloeCustomIcons"
font.fullname = "Floe Custom Icons"
font.copyright = "Copyright 2026 Sam Windell, CC-BY-SA-4.0"
# Match fa-solid-900.ttf so merged glyphs share its scale and baseline.
font.em = 512
font.ascent = 448
font.descent = 64
font.hhea_ascent_add = False
font.hhea_descent_add = False
font.os2_typoascent_add = False
font.os2_typodescent_add = False
font.os2_winascent_add = False
font.os2_windescent_add = False
font.hhea_ascent = 459
font.hhea_descent = -75
font.os2_typoascent = 459
font.os2_typodescent = -75
font.os2_winascent = 459
font.os2_windescent = 75

for name, codepoint in icons.items():
    glyph = font.createChar(codepoint, name)
    glyph.importOutlines(os.path.join(here, name + ".svg"), scale=False)
    glyph.width = 512
    glyph.removeOverlap()
    glyph.correctDirection()

font.generate(os.path.join(here, "custom_icons.ttf"))
