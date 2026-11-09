"""release_pages.py --releases <GitHub releases JSON> --notes <newest docs/RELEASE-NOTES-x.md> [--android <RELEASE-BODY.md> --android-tag <tag>] --out <folder>

The release pages in one layout, like the README: a one-line summary, download cards for this release's files, what's new in this
release only (not every older version again), this version at a glance, and for the newest release of each line its known issues and
upgrade steps, folded away; links, the free-forever line and the checksums at the end. Writes <tag>.md (the body) for every release
and index.json (tag, title, date, pre-release, latest) for the release forms and the preview. The Discord post reads the top of a body
up to the first older-version heading, so the summary and "What's new in <version>" come first."""
import argparse, json, pathlib, re

REPO = 'https://github.com/xFractals/pokopia-native'
ART = 'https://raw.githubusercontent.com/xFractals/pokopia-native/main/docs/assets/release'
GUIDE = REPO + '/blob/main/docs/'
LINUX_GUIDE = GUIDE + 'LINUX.md'   # on main from 1.4.0, when Linux left beta
ANDROID_GUIDE = REPO + '/blob/android-beta/docs/ANDROID.md'

# tag: (title, one-line summary)
PAGES = {
    'v1.4.0': ('Pokopia Native 1.4.0: Linux and the Steam Deck out of beta, and your own DLC',
               'Linux and the Steam Deck as a full release, your own DLC on Windows and Linux, smoother edges, a smoother frame rate when the '
               'processor is the limit, split-screen co-op on one PC (experimental), and a Play Pokopia program you can pin or add to Steam.'),
    'v1.3.5': ('Pokopia Native 1.3.5: controller rebinding, Link Play in the game, automatic port forwarding',
               'Change your controller buttons and run Link Play from inside the game on Windows, with automatic port forwarding when you host, '
               'many more controllers, NVIDIA and AMD freeze fixes, and a smoother camera while walking sideways.'),
    'v1.3.4': ('Pokopia Native 1.3.4: Wide view for ultrawide screens',
               'Wide view for 21:9 and wider screens (experimental), the mouse camera at full speed while you strafe, and a clear message when '
               'Windows finds no graphics card the game can use.'),
    'v1.3.3': ('Pokopia Native 1.3.3: co-op on game version 1.0.5',
               'Interact everywhere and Story co-op on game version 1.0.5 too, the mouse camera while walking sideways, and fixes for another '
               'NVIDIA freeze and for lag near water.'),
    'v1.3.2': ('Pokopia Native 1.3.2: the AMD crash fixed', "A hotfix for 1.3.1's crash on AMD graphics cards."),
    'v1.3.1': ('Pokopia Native 1.3.1: another NVIDIA freeze fixed',
               'Another NVIDIA freeze fixed, builds in folders with accented names, .NET found right after you install it, and a test mode '
               'for AMD and Intel graphics.'),
    'v1.3.0': ('Pokopia Native 1.3.0: higher resolution and settings in the game',
               'The game drawn at up to 4K with settings inside the game on Windows, a step-by-step builder on Windows and the Mac, and Story '
               'co-op (experimental).'),
    'v1.2.6': ('Pokopia Native 1.2.6: particle effects drawn',
               'Particle effects that had to be skipped are drawn again, a Pokopia Settings launcher on Windows, and fixes for Windows and the Mac.'),
    'v1.2.5': ('Pokopia Native 1.2.5: the RTX 50 freeze fixed', 'Fixes the game freezing on RTX 50-series graphics cards.'),
    'v1.2.4': ('Pokopia Native 1.2.4: freezes when effects start fixed',
               'Fixes freezes when certain particle effects start, with a report when the graphics card stops responding.'),
    'v1.2.3': ('Pokopia Native 1.2.3: sharp upscaling and display settings',
               'A sharp upscaler for big screens and a Display tab on Windows: console mode, window or full screen, frame rate and vsync.'),
    'v1.2.2': ('Pokopia Native 1.2.2: AMD and controller fixes',
               "Characters drawn on AMD graphics cards, and the name question answered with a controller."),
    'v1.2.1': ('Pokopia Native 1.2.1: the Mac Python fix', "Fixes installing the builder's Python packages on the Mac."),
    'v1.2.0': ('Pokopia Native 1.2.0: Interact everywhere and keyboard controls',
               'Visitors can build and use their moves in every town (experimental), and the keyboard can be rebound on Windows.'),
    'v1.1.2': ('Pokopia Native 1.1.2: the new-game fix and a smoother first play',
               'New games get past the name question on Windows, and the first play-through freezes far less.'),
    'v1.1.1': ('Pokopia Native 1.1.1: a builder fix and .nup dumps',
               'No more terminal windows during the shader step, and .nup dump files work as your dump.'),
    'v1.1.0': ('Pokopia Native 1.1.0: native on Windows, and Link Play',
               'The game recompiled natively for Windows, and Link Play between Windows, Mac and iPhone.'),
    'v1.0.3': ('Pokopia Native 1.0.3: a Mac installer fix', 'Fixes for the Mac installer and the shader refresh.'),
    'v1.0.2': ('Pokopia Native 1.0.2: icons and the launcher', 'App icon and launcher fixes.'),
    'v1.0.1': ('Pokopia Native 1.0.1: icons', 'An app icon fix.'),
    'v1.0.0': ('Pokopia Native 1.0.0: Mac and iPhone', 'Pokopia built natively for the Mac and the iPhone from your own dump.'),
    'v1.3.3-linux-beta.5': ('Linux and Steam Deck, beta 5',
                            'No more sparkles on 16:10 screens, a steadier frame rate on integrated graphics, a builder that looks right, and a '
                            'report tool that finds your build by itself.'),
    'v1.3.3-linux-beta.4': ('Linux and Steam Deck, beta 4', 'Version 1.3.3 on Linux and the Steam Deck.'),
    'v1.3.0-linux-beta.3': ('Linux and Steam Deck, beta 3', 'Crackle-free sound on PipeWire.'),
    'v1.3.0-linux-beta.2': ('Linux and Steam Deck, beta 2', 'Starts on the Steam Deck, Fedora, Arch and more.'),
    'v1.3.0-linux-beta': ('Linux and Steam Deck: the 1.3.0 beta', 'Higher resolution and settings in the game, on Linux.'),
    'v1.2.6-linux-beta': ('Linux and Steam Deck: the first open beta', 'Pokopia Native on Linux and the Steam Deck, as an open beta.'),
    'v1.3.3-android-beta.2': ('Android, beta 2: the first public Android beta',
                              'Pokopia Native as an Android app for phones and handhelds, made from your own copy on a Windows PC and put on the '
                              'phone with a USB cable.'),
}

# The overarching features: (area, the version it arrived in, what it is). "This version at a glance" lists what a version has.
FEATURES = [   # {W} reads "Windows" before 1.4.0 and "Windows and Linux" from 1.4.0, when Linux left beta with the Windows game's features
    ('Devices', '1.0.0', 'Mac and iPhone'), ('Devices', '1.1.0', 'Windows, recompiled natively'), ('Devices', '1.4.0', 'Linux and the Steam Deck'),
    ('Playing together', '1.1.0', 'Link Play between Windows, Mac and iPhone, up to four players'),
    ('Playing together', '1.2.0', 'Interact everywhere (experimental)'), ('Playing together', '1.3.0', 'Story co-op (experimental)'),
    ('Playing together', '1.3.3', 'co-op on game version 1.0.5 too'),
    ('Playing together', '1.3.5', 'Link Play settings and Copy invite in the game ({W})'),
    ('Playing together', '1.3.5', 'automatic port forwarding ({W})'),
    ('Playing together', '1.4.0', 'split screen on one PC (experimental, Windows)'),
    ('Graphics', '1.2.3', 'sharp upscaling and display settings ({W})'),
    ('Graphics', '1.3.0', 'up to 4K, with settings in the game on F1 ({W})'), ('Graphics', '1.3.4', 'Wide view for 21:9 (experimental, Windows)'),
    ('Graphics', '1.4.0', 'anti-aliasing, SMAA or FXAA ({W})'), ('Graphics', '1.4.0', 'a graphics thread for a smoother frame rate ({W})'),
    ('Controls', '1.0.0', 'drag to look (Mac and iPhone)'), ('Controls', '1.1.0', 'mouse look and controllers ({W})'),
    ('Controls', '1.2.0', 'keyboard rebinding ({W})'), ('Controls', '1.3.5', 'controller rebinding and player 1 choice ({W})'),
    ('Controls', '1.3.5', "many more controllers, through SDL's community list ({W})"),
    ('Builder and saves', '1.0.0', 'built from your own dump'), ('Builder and saves', '1.0.0', 'the iPhone app from a Mac or a Windows PC'),
    ('Builder and saves', '1.1.0', 'bring your save over ({W})'), ('Builder and saves', '1.1.1', '.nup dumps'),
    ('Builder and saves', '1.3.0', 'a step-by-step builder'), ('Builder and saves', '1.4.0', 'your own DLC ({W})'),
    ('Builder and saves', '1.4.0', 'Play Pokopia as a program to pin or add to Steam (Windows)'),
]
LINUX_SKIP = ('Mac and iPhone', 'drag to look', 'the iPhone app', 'Link Play settings', 'automatic port forwarding', 'Wide view')

def ver(s): return tuple(int(x) for x in re.match(r'v?(\d+)\.(\d+)\.(\d+)', s).groups())

def sections(md, level=2):
    """{heading: text} for the headings of this level (text up to the next heading of the same or a higher level)"""
    out, cur, buf = {}, None, []
    for line in md.split('\n'):
        m = re.match(r'^(#{1,%d}) (.*?)\s*$' % level, line)
        if m:
            if cur is not None: out[cur] = '\n'.join(buf).strip('\n')
            cur, buf = (m.group(2) if len(m.group(1)) == level else None), []
        elif cur is not None: buf.append(line)
    if cur is not None: out[cur] = '\n'.join(buf).strip('\n')
    return out

def drop_subsection(md, title):
    return re.sub(r'^### ' + re.escape(title) + r'\n.*?(?=^### |\Z)', '', md, flags=re.S | re.M)

def drop_linux_lines(md):
    md = drop_subsection(md, 'Linux')
    md = re.sub(r'^- \*\*Linux and the Steam Deck:\*\*.*?(?=^- |^### |^\S|\Z)', '', md, flags=re.S | re.M)
    return md.replace('### Mac, iPhone and Linux', '### Mac and iPhone')

def clean(md):
    """Absolute links (a release page resolves no relative ones), no emulator names, tidy blank lines."""
    md = md.replace('\r', '')
    md = re.sub(r'\]\((?!https?:|#|mailto:)(\.\./)?(docs/|LEGAL\.md|README\.md|windows/)', lambda m: '](' + REPO + '/blob/main/' + m.group(2), md)
    md = re.sub(r"\s*\(`--eden`[^)]*\)", '', md)
    md = re.sub(r'\bEden(?:,| or| and)\s+', '', md); md = re.sub(r'(?:,| or| and)\s+Eden\b', '', md)
    md = re.sub(r'\(under New in (\d+\.\d+\.\d+)\)', lambda m: f'(see [{m.group(1)}]({REPO}/releases/tag/v{m.group(1)}))', md)   # each page has its own notes now
    md = re.sub(r'"Getting it" under (\d+\.\d+\.\d+)', lambda m: f'"Getting it" in [{m.group(1)}]({REPO}/releases/tag/v{m.group(1)})', md)
    md = re.sub(r'\n{3,}', '\n\n', md).strip()
    bad = re.search(r'(?i)\beden\b|eglantine|lined up screenshots|switch 2 footage', md)
    if bad: raise SystemExit(f'left in a body: ...{md[max(0, bad.start() - 80):bad.end() + 80]}...')
    return md

def card(kind, href, alt):
    return (f'<a href="{href}"><picture><source media="(prefers-color-scheme: dark)" srcset="{ART}/get-{kind}-dark.png">'
            f'<img src="{ART}/get-{kind}-light.png" width="404" alt="{alt}"></picture></a>')

def assets_of(body):
    m = re.search(r'## Checksums \(SHA-256\)\s*```[^\n]*\n(.*?)```', body.replace('\r', ''), re.S)
    block = m.group(1).strip() if m else ''
    return block, [l.split()[-1] for l in block.splitlines() if l.strip()]

def downloads(tag, names):
    cards, extra = [], []
    order = lambda n: 0 if 'Windows' in n else 1 if 'Mac' in n else 2 if n.endswith('.AppImage') else 3   # Windows, Mac, then Linux's AppImage first
    for n in sorted(names, key=order):
        url = f'{REPO}/releases/download/{tag}/{n}'
        if n == 'Pokopia-Native-for-Windows.zip': cards.append(card('windows', url, 'Download for Windows'))
        elif n == 'Pokopia-Native-for-Mac.zip': cards.append(card('mac', url, 'Download for Mac and iPhone'))
        elif n.endswith('.AppImage') and not any('get-linux' in c for c in cards): cards.append(card('linux', url, 'Get the Linux and Steam Deck beta (AppImage)'))
        elif 'Linux' in n and n.endswith('.zip'):
            if any('get-linux' in c for c in cards): extra.append(f"Prefer a terminal, or the AppImage won't start? Use [`{n}`]({url}) instead.")
            else: cards.append(card('linux', url, 'Get the Linux and Steam Deck beta'))
        elif 'Android' in n and n.endswith('.zip'): cards.append(card('android', url, 'Get the Android beta'))
        elif n.endswith('.apk'): extra.append(f'**Check your phone first:** the [device test]({url}) (`{n}`) says in seconds whether it can run the game.')
        else: extra.append(f'Also here: [`{n}`]({url})')
    if not cards: return ''
    rows = [cards[i:i + 2] for i in range(0, len(cards), 2)]
    return '<p align="center">\n' + '\n  <br>\n'.join('  ' + '\n  '.join(r) for r in rows) + '\n</p>' + ('\n\n' + '\n\n'.join(extra) if extra else '')

def glance(v, linux=False):
    lines = []
    for area in dict.fromkeys(a for a, _, _ in FEATURES):
        got = [(s, f) for a, s, f in FEATURES if a == area and ver(s) <= v and not (linux and any(k in f for k in LINUX_SKIP))]
        if linux and area == 'Devices': got = [('0.0.0', 'Linux and the Steam Deck (beta)')]
        if not got: continue
        where = 'Linux' if linux else 'Windows and Linux' if v >= (1, 4, 0) else 'Windows'
        items = [(f'**new:** {f}' if ver(s) == v and not linux else f).replace('{W}', where) for s, f in got]
        lines.append(f'- **{area}:** ' + '; '.join(items) + '.')
    if linux: lines.append('- Link Play on Linux joins a host by its address; finding hosts on the same network comes later.')
    return '\n'.join(lines)

def folded(title, md):
    return f'<details>\n<summary><b>{title}</b></summary>\n\n{md}\n\n</details>' if md.strip() else ''

def footer(checksums):
    guides = ' · '.join(f'[{n}]({u})' for n, u in (('Windows', GUIDE + 'WINDOWS.md'), ('Mac', GUIDE + 'MAC.md'), ('iPhone', GUIDE + 'IPHONE.md'),
                        ('Android', ANDROID_GUIDE), ('Linux & Steam Deck', LINUX_GUIDE), ('Playing together', GUIDE + 'PLAYING-TOGETHER.md'),
                        ('Common problems', GUIDE + 'TROUBLESHOOTING.md')))
    out = (f'---\n\n**Guides:** {guides}  \n**Questions?** Ask on [Discord](https://discord.gg/Aj9GqxZj8V) · '
           f'**Found a bug?** [Open an issue]({REPO}/issues/new/choose)\n\n'
           '<sub>Pokopia Native is free, and always will be: if anyone asks you to pay for it, it isn\'t us. Bring your own game: nothing of '
           'the game is in these downloads, and everything is built on your computer from your own dump.</sub>')
    if checksums: out += '\n\n' + folded('Checksums (SHA-256)', f'```\n{checksums}\n```')
    return out

def page(tag, summary, cards, new_title, new_md, glance_md, more, checksums):
    parts = [f'**{summary}**']
    if cards: parts.append(cards)
    parts += [f'## {new_title}', new_md]
    if glance_md: parts += ['## This version at a glance', glance_md]
    parts += [m for m in more if m]
    parts.append(footer(checksums))
    return '\n\n'.join(p.strip() for p in parts if p and p.strip()) + '\n'

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--releases', required=True); ap.add_argument('--notes', required=True)
    ap.add_argument('--android'); ap.add_argument('--android-tag'); ap.add_argument('--out', required=True)
    ap.add_argument('--new-tag', help='a release not published yet (its page from --notes)'); ap.add_argument('--new-assets', nargs='*', default=[])
    ap.add_argument('--new-sums', help="its SHA256SUMS.txt, once the downloads are packaged"); a = ap.parse_args()
    rel = json.loads(pathlib.Path(a.releases).read_text(encoding='utf-8'))
    notes = sections(pathlib.Path(a.notes).read_text(encoding='utf-8').replace('\r', ''))
    stable = [r['tag_name'] for r in rel if not r.get('prerelease')] + ([a.new_tag] if a.new_tag else [])
    newest_stable = max(stable, key=ver)
    newest_linux = max((r['tag_name'] for r in rel if 'linux-beta' in r['tag_name']), key=lambda t: (ver(t), t))
    out = pathlib.Path(a.out); out.mkdir(parents=True, exist_ok=True); index = []
    items = [(r['tag_name'], (r.get('body') or '').replace('\r', ''), r.get('published_at', '')[:10], bool(r.get('prerelease'))) for r in rel]
    if a.android: items.insert(0, (a.android_tag, pathlib.Path(a.android).read_text(encoding='utf-8').replace('\r', ''), '', True))
    if a.new_tag:   # its downloads and checksums go in the body the way the published pages carry them
        sums = pathlib.Path(a.new_sums).read_text(encoding='utf-8').strip() if a.new_sums else '\n'.join(f'{"0" * 64}  {n}' for n in a.new_assets)
        items.insert(0, (a.new_tag, f'## Checksums (SHA-256)\n\n```\n{sums}\n```\n', '', False))
    for tag, body, date, pre in items:
        title, summary = PAGES[tag]; checksums, names = assets_of(body); v = ver(tag); bs = sections(body)
        num = '.'.join(map(str, v))
        if 'android-beta' in tag:
            new = bs.get('What changed since the private beta 1', '') + "\n\n### What's in it\n\n" + bs.get("What's in it", '')
            more = [folded('Getting started', bs.get('Getting started', '')), folded("What's tested, and what isn't", bs.get("What's tested, and what isn't", '')),
                    folded('Known issues', bs.get('Known', '')), folded('Reporting: what helps', bs.get('Reporting: what helps', ''))]
            gl = ("- **Built on 1.3.3**, with the Windows game's recompiler and Vulkan renderer, for arm64 phones and handhelds.\n"
                  "- **Controls:** touch controls on the screen, or a Bluetooth or USB controller, or a handheld's own buttons.\n"
                  '- **Settings:** Back opens them (resolution, frame rate, controls).\n'
                  '- **Not in this beta yet:** Link Play, and bringing a save over.')
            body_md = page(tag, summary, downloads(tag, names), "What's new in Android beta 2", new, gl, more, checksums)
        elif 'linux-beta' in tag:
            key = next((k for k in bs if re.match(r"(?i)(what changed|what's new|new in)", k)), None)
            n = re.search(r'beta\.?(\d*)$', tag).group(1) or '1'
            new = bs[key] if key else re.split(r'^## ', body, maxsplit=1, flags=re.M)[0]
            latest = tag == newest_linux
            known = bs.get('Known', '') + ('\n\n' + bs['Still open'] if 'Still open' in bs else '')
            more = ([folded('Getting started', bs.get('Getting started', '')), folded('Known issues', known),
                     folded('Reporting: what helps', bs.get('Reporting: what helps', ''))] if latest else [])
            need = ('For **x86-64** Linux PCs and the Steam Deck: a CPU with AVX2 and FMA3 (most from about 2015 on), a Vulkan 1.3 driver (SteamOS has '
                    'one), about 15 GB free, and your own dump of the game.')
            body_md = page(tag, summary, downloads(tag, names) + '\n\n' + need, f"What's new in this beta (built on {num})", new, glance(v, linux=True), more, checksums)
        else:
            new = notes.get(f'New in {num}') or re.split(r'^## ', body, maxsplit=1, flags=re.M)[0]
            new = drop_linux_lines(new)
            latest = tag == newest_stable
            more = [folded('Known issues', notes.get('Known issues', '')), folded('Upgrading from an older version', notes.get('Upgrading', ''))] if latest else []
            body_md = page(tag, summary, downloads(tag, names), f"What's new in {num}", new, glance(v), more, checksums)
        body_md = clean(body_md)
        (out / f'{tag}.md').write_bytes(body_md.encode('utf-8'))
        index.append({'tag': tag, 'title': title, 'date': date, 'prerelease': pre, 'latest': tag == newest_stable, 'chars': len(body_md),
                      'published': bool(date)})
    (out / 'index.json').write_text(json.dumps(index, indent=1) + '\n', encoding='utf-8')
    print(f'{len(index)} pages in {out}; longest {max(i["chars"] for i in index):,} characters (the old 1.3.5 page was 44,403)')

if __name__ == '__main__':
    main()
