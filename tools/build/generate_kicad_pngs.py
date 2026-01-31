#!/usr/bin/env python3

# Regenerate icon_kicad PNG files with black background

import os
from subprocess import call

# Source images
APPLE_DARK_SVG = "/Users/jared/Documents/kicadpp/images/apple-dark.svg"
OUTPUT_DIR = "../../resources/bitmaps_png/png"

# All the sizes we need
SIZES = [16, 24, 32, 48, 64, 128, 256]

# Size combinations for multi-resolution icons (iconsize, outputsize)
SIZE_COMBOS = [
    (16, 16), (16, 32),
    (24, 16), (24, 24), (24, 32), (24, 48), (24, 64),
    (32, 32),
]

def generate_png(output_path, size):
    """Generate a PNG from SVG at the specified size."""
    print(f"Generating {output_path} at {size}x{size}")
    call(["rsvg-convert", "--width={}".format(size), "--height={}".format(size), 
          "--output={}".format(output_path), APPLE_DARK_SVG])

if __name__ == '__main__':
    # Generate basic size variants (both light and dark use same apple-dark source)
    for size in SIZES:
        # Regular versions
        generate_png(os.path.join(OUTPUT_DIR, f"icon_kicad_{size}.png"), size)
        generate_png(os.path.join(OUTPUT_DIR, f"icon_kicad_dark_{size}.png"), size)
        
        # Nightly versions (same design for now)
        generate_png(os.path.join(OUTPUT_DIR, f"icon_kicad_nightly_{size}.png"), size)
        generate_png(os.path.join(OUTPUT_DIR, f"icon_kicad_nightly_dark_{size}.png"), size)
    
    # Generate size combination variants
    for icon_size, output_size in SIZE_COMBOS:
        # Regular versions
        generate_png(os.path.join(OUTPUT_DIR, f"icon_kicad_{icon_size}_{output_size}.png"), output_size)
        generate_png(os.path.join(OUTPUT_DIR, f"icon_kicad_{icon_size}_dark_{output_size}.png"), output_size)
        
        # Nightly versions
        generate_png(os.path.join(OUTPUT_DIR, f"icon_kicad_nightly_{icon_size}_{output_size}.png"), output_size)
        generate_png(os.path.join(OUTPUT_DIR, f"icon_kicad_nightly_{icon_size}_dark_{output_size}.png"), output_size)
    
    print("Done!")
