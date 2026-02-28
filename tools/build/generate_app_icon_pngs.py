#!/usr/bin/env python3

# Regenerate PNG files for agent and vcs app icons

import os
from subprocess import call

SOURCES_DARK = "../../resources/bitmaps_png/sources/dark/"
SOURCES_LIGHT = "../../resources/bitmaps_png/sources/light/"
OUTPUT_DIR = "../../resources/bitmaps_png/png"
ICONS_DIR = "../../resources/bitmaps_png/icons"

# Icons to generate (name without icon_ prefix)
ICONS = ["agent", "vcs", "terminal"]

# All the sizes we need
SIZES = [16, 24, 32, 48, 64, 128, 256]

# Size combinations for multi-resolution icons (iconsize, outputsize)
SIZE_COMBOS = [
    (16, 16), (16, 32),
    (24, 16), (24, 24), (24, 32), (24, 48), (24, 64),
    (32, 32),
]

def generate_png(output_path, size, svg_source):
    """Generate a PNG from SVG at the specified size."""
    print(f"Generating {output_path} at {size}x{size}")
    call(["rsvg-convert", "--width={}".format(size), "--height={}".format(size),
          "--output={}".format(output_path), svg_source])

if __name__ == '__main__':
    for icon_name in ICONS:
        svg_dark = SOURCES_DARK + "icon_" + icon_name + ".svg"
        svg_light = SOURCES_LIGHT + "icon_" + icon_name + ".svg"

        if not os.path.exists(svg_dark):
            print(f"Warning: {svg_dark} not found, skipping")
            continue

        print(f"\nGenerating PNGs for {icon_name}...")

        # Generate basic size variants
        for size in SIZES:
            # Regular (for light theme UI, uses light source)
            generate_png(os.path.join(OUTPUT_DIR, f"icon_{icon_name}_{size}.png"), size, svg_light)
            # Dark theme variant (for dark theme UI, uses dark source)
            generate_png(os.path.join(OUTPUT_DIR, f"icon_{icon_name}_dark_{size}.png"), size, svg_dark)

        # Generate size combination variants
        for icon_size, output_size in SIZE_COMBOS:
            # Regular versions (for light theme UI)
            generate_png(os.path.join(OUTPUT_DIR, f"icon_{icon_name}_{icon_size}_{output_size}.png"), output_size, svg_light)
            # Dark versions (for dark theme UI)
            generate_png(os.path.join(OUTPUT_DIR, f"icon_{icon_name}_{icon_size}_dark_{output_size}.png"), output_size, svg_dark)

        # Generate icons directory files (use light source for default)
        generate_png(os.path.join(ICONS_DIR, f"icon_{icon_name}.png"), 128, svg_light)
        generate_png(os.path.join(ICONS_DIR, f"icon_{icon_name}_64.png"), 64, svg_light)

    print("\nDone!")
