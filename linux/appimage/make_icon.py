"""The AppImage's icon, drawn here (nothing from the game): a green rounded square with "PN" in a bold sans face.
   make_icon.py <out.png> <bold .ttf>"""
import sys
from PIL import Image, ImageDraw, ImageFont

out, font = sys.argv[1:3]
S = 256
im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
d.rounded_rectangle((8, 8, S - 8, S - 8), radius=48, fill=(46, 139, 87, 255))
d.rounded_rectangle((8, 8, S - 8, S // 2), radius=48, fill=(60, 160, 100, 255))
d.rectangle((8, S // 2 - 48, S - 8, S // 2), fill=(60, 160, 100, 255))
f = ImageFont.truetype(font, 110)
x0, y0, x1, y1 = d.textbbox((0, 0), 'PN', font=f)
d.text(((S - (x1 - x0)) / 2 - x0, (S - (y1 - y0)) / 2 - y0), 'PN', font=f, fill=(255, 255, 255, 255))
im.save(out)
