#!/usr/bin/env python3

# Generate .icns files from PNG source for the new taskbar icon

import os
from shutil import copy, rmtree
from subprocess import call

# Use the padded apple icon PNG with black rounded background (1024x1024 for quality)
PNG_SOURCE = "/Users/jared/Documents/kicadpp/images/apple-icon-padded.png"

# (icon_name, output_dirs)
# Only generate main app icons with Zeo branding
ICONS = [
    ("agent", ["../../agent"]),
    ("kicad", ["../../kicad"]),
    ("kicad_doc", ["../../kicad"]),
    ("terminal", ["../../terminal"]),
]

# Sizes needed for .icns (size, iconset_name)
SIZES = [
    (16, "icon_16x16.png"),
    (32, "icon_16x16@2x.png"),
    (32, "icon_32x32.png"),
    (64, "icon_32x32@2x.png"),
    (128, "icon_128x128.png"),
    (256, "icon_128x128@2x.png"),
    (256, "icon_256x256.png"),
    (512, "icon_256x256@2x.png"),
    (512, "icon_512x512.png"),
    (1024, "icon_512x512@2x.png"),
]

if __name__ == '__main__':
    for icon_name, output_dirs in ICONS:
        iconset = icon_name + ".iconset"
        
        # Create iconset directory
        if os.path.exists(iconset):
            rmtree(iconset)
        os.mkdir(iconset)
        
        # Generate each size using sips
        for size, filename in SIZES:
            dest_path = os.path.join(iconset, filename)
            print(f"Generating {dest_path} at {size}x{size}")
            call(["sips", "-z", str(size), str(size), PNG_SOURCE, "--out", dest_path])
        
        # Convert iconset to .icns
        icns_file = icon_name + ".icns"
        print(f"Creating {icns_file}")
        call(["iconutil", "-c", "icns", iconset])
        
        # Copy to output directories
        for output_dir in output_dirs:
            dest = os.path.join(output_dir, icns_file)
            print(f"Copying to {dest}")
            copy(icns_file, dest)
        
        # Clean up
        rmtree(iconset)
        os.remove(icns_file)
        
    print("Done!")
