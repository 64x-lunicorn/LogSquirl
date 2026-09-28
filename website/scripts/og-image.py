#!/usr/bin/env python3
"""Draws public/og.png, the preview image a shared link to the website shows
(#582): the app icon beside a terminal window in the SMYCK colors of the Smyck
themes, 1200x630. Run from website/ with Pillow on macOS (it takes Menlo and
Helvetica from the system):  python3 scripts/og-image.py"""
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 630
WINDOW, BAR, BORDER = "#1b1b1b", "#242424", "#3a3a3a"
TEXT, MUTED, BLUE, GREEN = "#f7f7f7", "#b0b0b0", "#9cd9f0", "#8eb33b"
DOTS = ["#c75646", "#d0b03c", "#8eb33b"]

mono = lambda size, bold=False: ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", size, index=1 if bold else 0)
sans = lambda size: ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", size)

image = Image.new("RGB", (W, H), "#121212")
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((40, 40, W - 40, H - 40), 18, fill=WINDOW, outline=BORDER, width=2)
draw.rounded_rectangle((41, 41, W - 41, 100), 18, fill=BAR)
draw.rectangle((41, 80, W - 41, 100), fill=BAR)
draw.line((41, 100, W - 41, 100), fill=BORDER, width=2)
for i, color in enumerate(DOTS):
    draw.ellipse((70 + i * 30, 62, 88 + i * 30, 80), fill=color)
draw.text((W / 2, 71), "~/logsquirl", font=mono(20), fill=MUTED, anchor="mm")

icon = Image.open("src/assets/logsquirl.png").convert("RGBA").resize((330, 330), Image.LANCZOS)
image.paste(icon, (90, 180), icon)

x = 470
draw.text((x, 190), "$ ", font=mono(30), fill=GREEN)
draw.text((x + draw.textlength("$ ", font=mono(30)), 190), "logsquirl huge.log", font=mono(30), fill=MUTED)
draw.text((x, 250), "LogSquirl", font=mono(96, bold=True), fill=BLUE)
draw.text((x, 380), "A fast, smart log file explorer.", font=sans(40), fill=TEXT)
draw.text((x, 440), "Open source · Windows · macOS · Linux", font=sans(30), fill=MUTED)

image.save("public/og.png", optimize=True)
