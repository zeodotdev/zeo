#!/usr/bin/env python3
"""
Creates a DMG background image with drag-to-install arrow.
Requires: pip install pillow

Usage: python3 create-dmg-background.py
Output: dmg-background.png (600x400)
"""

import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Error: Pillow not installed. Install with: pip3 install pillow")
    sys.exit(1)

# Dimensions matching typical DMG window
WIDTH = 600
HEIGHT = 400

# Colors (dark theme to match Zeo branding)
BG_COLOR = (26, 26, 26)  # #1a1a1a
ARROW_COLOR = (74, 158, 255)  # #4a9eff (Zeo blue)
TEXT_COLOR = (100, 100, 100)

def create_background():
    # Create image with dark background
    img = Image.new('RGB', (WIDTH, HEIGHT), BG_COLOR)
    draw = ImageDraw.Draw(img)
    
    # Icon positions (same as set in AppleScript)
    left_x = 150   # Zeo.app position
    right_x = 450  # Applications position
    icon_y = 200   # Vertical center for icons
    
    # Draw arrow from app to Applications
    arrow_y = icon_y
    arrow_start = left_x + 64 + 20  # After app icon
    arrow_end = right_x - 64 - 20   # Before Applications icon
    
    # Arrow shaft
    draw.line([(arrow_start, arrow_y), (arrow_end - 20, arrow_y)], 
              fill=ARROW_COLOR, width=6)
    
    # Arrow head
    arrow_head = [
        (arrow_end, arrow_y),
        (arrow_end - 25, arrow_y - 15),
        (arrow_end - 25, arrow_y + 15)
    ]
    draw.polygon(arrow_head, fill=ARROW_COLOR)
    
    # Instruction text at bottom
    text = "Drag Zeo to Applications to install"
    try:
        # Try to use system font
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 14)
    except (IOError, OSError):
        font = ImageFont.load_default()
    
    # Get text bounding box for centering
    bbox = draw.textbbox((0, 0), text, font=font)
    text_width = bbox[2] - bbox[0]
    text_x = (WIDTH - text_width) // 2
    text_y = 340
    
    draw.text((text_x, text_y), text, fill=TEXT_COLOR, font=font)
    
    # Save into dev/assets/ (sibling of dev/utils/)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    output_path = os.path.join(script_dir, "..", "assets", "dmg-background.png")
    img.save(output_path, "PNG")
    print(f"Created: {output_path}")
    print(f"Size: {WIDTH}x{HEIGHT}")

if __name__ == "__main__":
    create_background()
