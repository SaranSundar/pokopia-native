"""make_readme_art.py [repository folder]: the README's artwork in docs/assets (hero, download cards, guide cards, Discord card,
how-it-works strip), in the fade colours (coral red -> hot pink -> magenta -> purple), light and dark, wide and phone.
The screenshots come from docs/images. Text uses the viewer's system font, so leave room for wider fonts. Needs Pillow."""
import base64, io, math, pathlib, sys
from PIL import Image

D = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent); A = D / 'docs' / 'assets'; A.mkdir(parents=True, exist_ok=True)
IMG = D / 'docs' / 'images'
FONT = "'Segoe UI Variable Display','Segoe UI',system-ui,-apple-system,BlinkMacSystemFont,'Helvetica Neue',Arial,sans-serif"
INK = '#1C1626'
FADE = ['#FF5E62', '#FF3D8E', '#D23CF0', '#8A3FFC']        # the fade, on light backgrounds
FADE_DK = ['#FF7A7E', '#FF5CA2', '#DE6BF5', '#A274FF']     # brighter, for text and lines on dark
ICON_FADE = ['#FF6A5E', '#FF3D8E', '#C93BF0', '#7B3FF7']   # the app icon's square
PAL = {
    'light': dict(bg0='#FFF3F7', bg1='#F4EFFF', card='#FFFFFF', line='#EAE2F2', ink=INK, muted='#6A6277', tint='#F5EDFF',
                  glow='#FFB0C8', glow_o=.6, glow2='#CDB0FF', glow2_o=.7, frame='#FFFFFF', shadow=.16, chip='#FFFFFF',
                  fade=FADE, pinkbg='#FFE3EC', pinkfg='#D4185A'),
    'dark': dict(bg0='#1F1528', bg1='#110F19', card='#17151F', line='#2F2A3B', ink='#F4F0FA', muted='#ABA3B8', tint='#271C37',
                 glow='#FF3D8E', glow_o=.2, glow2='#8A3FFC', glow2_o=.38, frame='#2A2336', shadow=.5, chip='#1F1A29',
                 fade=FADE_DK, pinkbg='#3D1628', pinkfg='#FF8FB0'),
}

def svg(w, h, body, defs=''):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" font-family="{FONT}">'
            f'<defs>{defs}</defs>{body}</svg>')

def text(x, y, s, size, fill, weight=400, anchor='start', ls=0):
    lsa = f' letter-spacing="{ls}"' if ls else ''
    return f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}"{lsa}>{s}</text>'

def grad(gid, cols, x1=0, y1=0, x2=1, y2=0, user=False):
    stops = ''.join(f'<stop offset="{i/(len(cols)-1):.3f}" stop-color="{c}"/>' for i, c in enumerate(cols))
    u = ' gradientUnits="userSpaceOnUse"' if user else ''
    return f'<linearGradient id="{gid}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"{u}>{stops}</linearGradient>'

def app_icon(x, y, s, uid):
    k = s / 220
    return (f'<g transform="translate({x},{y}) scale({k:.4f})">' + grad(f'sq{uid}', ICON_FADE, 0, 0, 1, 1)
            + f'<clipPath id="lf{uid}"><circle cx="0" cy="58" r="92"/></clipPath>'
            f'<rect width="220" height="220" rx="50" fill="url(#sq{uid})"/>'
            f'<g transform="translate(110,117) rotate(-40)"><g clip-path="url(#lf{uid})"><circle cx="0" cy="-58" r="92" fill="#FFFFFF"/></g>'
            f'<rect x="-94" y="-5" width="30" height="10" rx="3" fill="#FFFFFF" opacity=".85"/>'
            f'<rect x="-64" y="-3" width="118" height="6" rx="3" fill="#E7B6FF"/></g></g>')

# line icons in a 64-unit box, stroked with the fade (#ig, in the icon's own units); `hole` fills shapes that sit over others
ICON_GRAD = lambda p: grad('ig', p['fade'], 4, 8, 60, 58, user=True)
def icon(kind, x, y, scale, hole):
    g = {
        'pc': '<rect x="5" y="8" width="54" height="36" rx="6"/><path d="M32 44v9M21 55h22"/>',
        'laptop': '<rect x="10" y="12" width="44" height="30" rx="5"/><path d="M4 49h56"/>',
        'phone': '<rect x="19" y="4" width="26" height="56" rx="8"/><path d="M28 52h8"/>',
        'mac': f'<rect x="7" y="12" width="40" height="27" rx="5"/><path d="M2 46h50"/><rect x="43" y="21" width="18" height="32" rx="5" fill="{hole}"/><path d="M49.5 47.5h5"/>',
        'droid': f'<rect x="6" y="6" width="24" height="44" rx="6"/><path d="M15 43h6"/><rect x="22" y="28" width="40" height="26" rx="10" fill="{hole}"/><rect x="32" y="33" width="20" height="16" rx="2.5"/><circle cx="27.5" cy="38.5" r="1.8"/><circle cx="56.5" cy="38.5" r="1.8"/>',
        'people': '<circle cx="22" cy="20" r="8"/><path d="M8 50c0-9 6-15 14-15s14 6 14 15"/><circle cx="44" cy="24" r="7"/><path d="M38 37c2-1 4-2 6-2 7 0 12 5 12 13"/>',
        'deck': '<rect x="3" y="18" width="58" height="30" rx="13"/><rect x="19" y="24" width="26" height="18" rx="3"/><circle cx="11" cy="29" r="2.6"/><circle cx="53" cy="29" r="2.6"/><path d="M9 39h4M51 37v4"/>',
        'cart': '<path d="M14 6h28l10 10v40a4 4 0 0 1-4 4H14a4 4 0 0 1-4-4V10a4 4 0 0 1 4-4z"/><rect x="18" y="18" width="24" height="18" rx="3"/><path d="M18 46h8M18 52h14"/>',
    }[kind]
    return (f'<g transform="translate({x},{y}) scale({scale})" fill="none" stroke="url(#ig)" stroke-width="4" '
            f'stroke-linecap="round" stroke-linejoin="round">{g}</g>')

def shot(name, box):
    im = Image.open(IMG / name).convert('RGB').crop(box)
    b = io.BytesIO(); im.save(b, 'JPEG', quality=87, optimize=True, progressive=True)
    return 'data:image/jpeg;base64,' + base64.b64encode(b.getvalue()).decode()

DAY = shot('iphone-day.png', (195, 28, 755, 343))       # crops keep the touch buttons out of frame
NIGHT = shot('iphone-night.png', (215, 25, 765, 335))
CLIP = IMG / 'hero-clip.gif'   # a short silent loop of play: it keeps moving inside the hero picture
BACK = 'data:image/gif;base64,' + base64.b64encode(CLIP.read_bytes()).decode() if CLIP.is_file() else DAY

def framed(x, y, w, h, href, p, uid, pad=8, r=24):
    return (f'<clipPath id="c{uid}"><rect x="{x+pad}" y="{y+pad}" width="{w-2*pad}" height="{h-2*pad}" rx="{r-pad+2}"/></clipPath>'
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{p["frame"]}" filter="url(#sh)"/>'
            f'<image x="{x+pad}" y="{y+pad}" width="{w-2*pad}" height="{h-2*pad}" href="{href}" preserveAspectRatio="xMidYMid slice" clip-path="url(#c{uid})"/>')

def chip(x, y, label, p, size=18):
    w = int(len(label) * size * .54) + 58
    return (f'<rect x="{x}" y="{y}" width="{w}" height="{size*2}" rx="{size}" fill="{p["chip"]}" stroke="{p["line"]}" stroke-width="1.5"/>'
            f'<circle cx="{x+24}" cy="{y+size}" r="6" fill="#FF3D8E"/>' + text(x + 40, y + size * 1.36, label, size, p['ink'], 600))

def backdrop(w, h, p, glows):
    d = (grad('bg', [p['bg0'], p['bg1']], 0, 0, 1, 1) + f'<clipPath id="card"><rect width="{w}" height="{h}" rx="36"/></clipPath>'
         f'<filter id="blur" x="-60%" y="-60%" width="220%" height="220%"><feGaussianBlur stdDeviation="70"/></filter>'
         f'<filter id="sh" x="-20%" y="-20%" width="140%" height="160%"><feDropShadow dx="0" dy="22" stdDeviation="24" flood-color="#000" flood-opacity="{p["shadow"]}"/></filter>')
    g = ''.join(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{p[c]}" opacity="{p[c + "_o"]}" filter="url(#blur)"/>' for cx, cy, r, c in glows)
    b = (f'<g clip-path="url(#card)"><rect width="{w}" height="{h}" fill="url(#bg)"/>{g}</g>'
         f'<rect x="1" y="1" width="{w-2}" height="{h-2}" rx="35" fill="none" stroke="{p["line"]}" stroke-width="2"/>')
    return d, b

def hero(m):
    p = PAL[m]; W, H = 1600, 760
    d, b = backdrop(W, H, p, [(1240, 220, 270, 'glow'), (1480, 700, 240, 'glow2')])
    d += grad('hf', p['fade'], 430, 0, 635, 0, user=True)
    b += app_icon(92, 88, 64, 'h') + text(176, 133, 'Pokopia Native', 30, p['ink'], 650)
    b += text(88, 300, 'Pokopia, <tspan fill="url(#hf)">native</tspan>', 80, p['ink'], 750, ls=-2.4)
    b += text(88, 388, 'on every device.', 80, p['ink'], 750, ls=-2.4)
    b += text(92, 464, 'Rebuilt from your own copy of the game for', 28, p['muted'])
    b += text(92, 504, 'Windows, Mac, iPhone, Android and Steam Deck.', 28, p['muted'])
    for i, (v, l) in enumerate([('60 fps', 'native, no emulator'), ('Up to 4K', 'on Windows and Linux'), ('4 players', 'across your devices')]):
        x = 92 + i * 258
        if i: b += f'<path d="M{x-28} 572v74" stroke="{p["line"]}" stroke-width="2"/>'
        b += text(x, 606, v, 38, p['ink'], 700, ls=-.6) + text(x, 641, l, 21, p['muted'])
    b += framed(968, 92, 560, 315, BACK, p, 'd') + framed(840, 330, 604, 340, NIGHT, p, 'n')
    b += chip(864, 648, 'Captured on an iPhone', p)
    return svg(W, H, b, d)

def hero_phone(m):
    p = PAL[m]; W, H = 800, 1000
    d, b = backdrop(W, H, p, [(640, 560, 250, 'glow'), (120, 980, 220, 'glow2')])
    d += grad('hf', p['fade'], 360, 0, 545, 0, user=True)
    b += app_icon(56, 56, 56, 'h') + text(128, 96, 'Pokopia Native', 28, p['ink'], 650)
    b += text(52, 206, 'Pokopia, <tspan fill="url(#hf)">native</tspan>', 68, p['ink'], 750, ls=-2)
    b += text(52, 282, 'on every device.', 68, p['ink'], 750, ls=-2)
    b += text(56, 350, 'Rebuilt from your own copy of the game for', 29, p['muted'])
    b += text(56, 390, 'Windows, Mac, iPhone, Android and Steam Deck.', 29, p['muted'])
    b += framed(176, 452, 568, 320, BACK, p, 'd') + framed(56, 600, 600, 338, NIGHT, p, 'n')
    return svg(W, H, b, d)

CARDS = {
    'windows': ('pc', 'Windows', 'Windows 10 and 11, 64-bit', False),
    'mac': ('mac', 'Mac &amp; iPhone', 'Apple silicon, plus the iPhone app', False),
    'linux': ('deck', 'Linux &amp; Steam Deck', 'Desktop Linux and SteamOS', True),
    'android': ('droid', 'Android', 'Phones and handhelds, made on a PC', True),
}

def card(m, key):
    p = PAL[m]; W, H = 540, 330; kind, name, sub, beta = CARDS[key]
    d = ICON_GRAD(p) + grad('btn', FADE[1:]) + grad('beta', FADE[:2])
    b = f'<rect x="1" y="1" width="{W-2}" height="{H-2}" rx="34" fill="{p["card"]}" stroke="{p["line"]}" stroke-width="2"/>'
    b += f'<rect x="34" y="34" width="92" height="92" rx="24" fill="{p["tint"]}"/>' + icon(kind, 48, 48, 1, p['tint'])
    if beta:
        b += f'<rect x="{W-34-112}" y="50" width="112" height="40" rx="20" fill="{p["pinkbg"]}"/>' + text(W - 34 - 56, 77, 'Beta', 20, p['pinkfg'], 700, 'middle')
    b += text(34, 190, name, 44, p['ink'], 700, ls=-.7) + text(34, 234, sub, 25, p['muted'])
    if beta:
        b += f'<rect x="34" y="260" width="244" height="50" rx="25" fill="url(#beta)"/>' + text(60, 293, 'Get the beta', 24, '#FFFFFF', 650)
        b += '<path d="M228 285h18M239 277l8 8-8 8" fill="none" stroke="#FFFFFF" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>'
    else:
        b += f'<rect x="34" y="260" width="210" height="50" rx="25" fill="url(#btn)"/>' + text(60, 293, 'Download', 24, '#FFFFFF', 650)
        b += '<path d="M211 275v19M203 286l8 8 8-8" fill="none" stroke="#FFFFFF" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>'
    return svg(W, H, b, d)

GUIDES = {
    'windows': ('pc', 'Windows guide', 'Set up, build and play'),
    'mac': ('laptop', 'Mac guide', 'Xcode, Python and the builder'),
    'iphone': ('phone', 'iPhone guide', 'Build, sign and install the app'),
    'linux': ('deck', 'Linux &amp; Steam Deck guide', 'Desktop Linux and SteamOS'),
    'android': ('droid', 'Android guide', 'Device test, build and install'),
    'together': ('people', 'Playing together', 'Link Play, room codes and co-op'),
}

def guide(m, key):
    p = PAL[m]; W, H = 808, 176; kind, title, sub = GUIDES[key]
    b = f'<rect x="1" y="1" width="{W-2}" height="{H-2}" rx="30" fill="{p["card"]}" stroke="{p["line"]}" stroke-width="2"/>'
    b += f'<rect x="34" y="34" width="108" height="108" rx="26" fill="{p["tint"]}"/>' + icon(kind, 50, 50, 1.2, p['tint'])
    b += text(172, 86, title, 36, p['ink'], 700, ls=-.5) + text(172, 128, sub, 26, p['muted'])
    b += (f'<circle cx="{W-66}" cy="88" r="30" fill="{p["tint"]}"/><path d="M{W-72} 74l14 14-14 14" fill="none" stroke="url(#chev)" '
          f'stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>')
    return svg(W, H, b, ICON_GRAD(p) + grad('chev', p['fade'][1:], W - 74, 74, W - 56, 102, user=True))

def bubbles(cx, cy, k=1):
    return (f'<g transform="translate({cx},{cy}) scale({k})"><circle r="60" fill="#FFFFFF" fill-opacity=".16"/>'
            f'<path d="M-30-22a12 12 0 0 1 12-12h40a12 12 0 0 1 12 12v24a12 12 0 0 1-12 12H-2l-16 14V14h0a12 12 0 0 1-12-12z" fill="#FFFFFF"/>'
            f'<circle cx="-8" cy="-10" r="4" fill="#C026D3"/><circle cx="6" cy="-10" r="4" fill="#C026D3"/><circle cx="20" cy="-10" r="4" fill="#C026D3"/></g>')

def discord(wide):
    W, H = (1600, 300) if wide else (800, 400)
    d = (grad('g', ['#FF5E62', '#FF3D8E', '#C23BF0', '#6E3FF5'], 0, 0, 1, 1) + f'<clipPath id="card"><rect width="{W}" height="{H}" rx="36"/></clipPath>'
         f'<filter id="blur" x="-60%" y="-60%" width="220%" height="220%"><feGaussianBlur stdDeviation="50"/></filter>')
    b = (f'<g clip-path="url(#card)"><rect width="{W}" height="{H}" fill="url(#g)"/>'
         f'<circle cx="{W-110}" cy="20" r="170" fill="#FFFFFF" opacity=".14" filter="url(#blur)"/><circle cx="{W*.55:.0f}" cy="{H+40}" r="150" fill="#FFB3D1" opacity=".25" filter="url(#blur)"/></g>')
    btn = '#6D28D9'
    if wide:
        b += bubbles(150, 150) + text(250, 140, 'Come hang out on Discord', 50, '#FFFFFF', 750, ls=-1)
        b += text(252, 190, 'Install help, co-op partners and release news.', 25, '#FFE8F2')
        b += f'<rect x="1150" y="106" width="350" height="88" rx="44" fill="#FFFFFF"/>' + text(1196, 161, 'Join the server', 30, btn, 700)
        b += f'<path d="M1420 150h28M1436 137l13 13-13 13" fill="none" stroke="{btn}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>'
    else:
        b += bubbles(700, 100, .85) + text(52, 112, 'Come hang out', 56, '#FFFFFF', 750, ls=-1) + text(52, 178, 'on Discord', 56, '#FFFFFF', 750, ls=-1)
        b += text(54, 232, 'Install help, co-op partners, release news.', 27, '#FFE8F2')
        b += f'<rect x="52" y="272" width="696" height="88" rx="44" fill="#FFFFFF"/>' + text(400, 327, 'Join the server', 31, btn, 700, 'middle')
        b += f'<path d="M548 316h26M562 304l12 12-12 12" fill="none" stroke="{btn}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>'
    return svg(W, H, b, d)

STEPS = [('cart', 'Your game dump', 'your own copy, 2.0.1 or 1.0.5'),
         ('app', 'Pokopia Native Builder', 'recompiles it on your computer'),
         ('mac', 'A native game', 'on your PC, Mac, phone or Deck')]

def node(x, y, w, h, i, p, size):
    kind, title, sub = STEPS[i]
    b = f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="28" fill="{p["card"]}" stroke="{p["line"]}" stroke-width="2"/>'
    b += f'<rect x="{x+28}" y="{y+28}" width="84" height="84" rx="22" fill="{p["tint"]}"/>'
    b += app_icon(x + 40, y + 40, 60, f's{i}') if kind == 'app' else icon(kind, x + 38, y + 38, 1, p['tint'])
    b += f'<circle cx="{x+w-46}" cy="{y+50}" r="22" fill="none" stroke="{p["line"]}" stroke-width="2"/>' + text(x + w - 46, y + 58, str(i + 1), 22, p['muted'], 650, 'middle')
    b += text(x + 28, y + 160, title, size, p['ink'], 700, ls=-.4) + text(x + 28, y + 160 + size * 1.2, sub, round(size * .8), p['muted'])
    return b

def arrow(x1, y1, x2, y2, p):
    a = math.atan2(y2 - y1, x2 - x1); hx, hy = x2 - 14 * math.cos(a - .5), y2 - 14 * math.sin(a - .5); kx, ky = x2 - 14 * math.cos(a + .5), y2 - 14 * math.sin(a + .5)
    return (f'<path d="M{x1} {y1}L{x2} {y2}" stroke="{p["muted"]}" stroke-width="3" stroke-dasharray="2 9" stroke-linecap="round"/>'
            f'<path d="M{hx:.1f} {hy:.1f}L{x2} {y2}L{kx:.1f} {ky:.1f}" fill="none" stroke="{p["muted"]}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>')

def flow(m, wide):
    p = PAL[m]
    if wide:
        W, H, b = 1600, 270, ''
        for i in range(3): b += node(8 + i * 554, 10, 476, 250, i, p, 32)
        for i in range(2): x = 8 + i * 554 + 476; b += arrow(x + 16, 135, x + 62, 135, p)
    else:
        W, H, b = 800, 940, ''
        for i in range(3): b += node(8, 10 + i * 312, 784, 252, i, p, 36)
        for i in range(2): y = 10 + i * 312 + 252; b += arrow(400, y + 8, 400, y + 54, p)
    return svg(W, H, b, ICON_GRAD(p))

out = {}
for m in PAL:
    out[f'hero-{m}.svg'] = hero(m); out[f'hero-phone-{m}.svg'] = hero_phone(m)
    out[f'flow-{m}.svg'] = flow(m, True); out[f'flow-phone-{m}.svg'] = flow(m, False)
    for k in CARDS: out[f'get-{k}-{m}.svg'] = card(m, k)
    for k in GUIDES: out[f'guide-{k}-{m}.svg'] = guide(m, k)
out['discord.svg'] = discord(True); out['discord-phone.svg'] = discord(False)
for n, s in out.items(): (A / n).write_bytes(s.encode('utf-8'))
print(len(out), 'files,', sum(len(s) for s in out.values()) // 1024, 'KB')
