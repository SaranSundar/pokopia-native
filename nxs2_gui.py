#!/usr/bin/env python3
"""nxs2_gui.py: the window around nxs2.py. A walkthrough takes you from "what do you want to make" through the checks, your game files and
the build to playing, in plain words, and remembers where you are (a refresh due after the first play, a new download to update to).
Advanced is the one-screen view with every button. Everything is done by nxs2.py underneath (the same commands the README lists), so a
build started here can be continued from Terminal and the other way round.

Mac: double-click "Pokopia Native Builder.app" (or: python3.12 nxs2_gui.py). Windows: "Pokopia Native Builder.cmd" (the Windows game and the iPhone app).
Needs Python 3.12+ with Tk (macOS Homebrew: brew install python-tk@3.12; python.org installers have it).
"""
import argparse, json, os, pathlib, queue, re, shutil, subprocess, sys, threading, time, webbrowser
import tkinter as tk
import tkinter.font as tkfont
from tkinter import filedialog, messagebox, ttk

HERE = pathlib.Path(__file__).resolve().parent
NXS2 = HERE / 'nxs2.py'
TITLE = '0400A3A01EE4C000'
NAME = 'Pokemon Pokopia'
MAC = sys.platform == 'darwin'
WIN = sys.platform == 'win32'
SETTINGS = pathlib.Path.home() / ('.nxs2_gui.json')
LOG = HERE / 'builder-log.txt'   # Windows: the launcher's start-up checks and the window's own errors, for a report
DISCORD = 'https://discord.gg/Aj9GqxZj8V'   # the community server: install help, bug reports, co-op partners
DOCS = 'https://github.com/xFractals/pokopia-native/blob/main/docs/'
PC = 'PC' if WIN else 'Mac' if MAC else 'computer'


def log_line(text):
    if not WIN: return
    try:
        with open(LOG, 'a', encoding='utf-8') as f: f.write(text.rstrip('\n') + '\n')
    except OSError: pass


def report_crash(where):   # the window has no console on Windows: an error goes to the log and on screen
    import traceback
    t = traceback.format_exc()
    log_line(f'{where}:\n{t}')
    try: messagebox.showerror('Pokopia Native Builder', f'{where}:\n\n{t[-1500:]}' + (f'\nThis is also in {LOG}.' if WIN else ''))
    except Exception: pass


def tool_version():
    m = re.search(r"^TOOL_VERSION = '([^']+)'", NXS2.read_text(encoding='utf-8'), re.M) if NXS2.is_file() else None
    return m.group(1) if m else '?'


def build_dir_for(dump):
    d = pathlib.Path(dump).expanduser()
    return d.parent / f'{d.stem if d.suffix.lower() == ".nup" else d.name}_native' if dump else None


# ---------------------------------------------------------------- the walkthrough's look
# The in-game settings menu's colours (windows-native src/nxrt/front/menu.c): a white-lavender panel, lavender pills, yellow for what you choose
C = {'bg': '#FAF8FF', 'edge': '#C8BAF6', 'title': '#423680', 'text': '#403C60', 'soft': '#827CA0', 'pill': '#EEE9FD', 'pill_hi': '#E5DDFB',
     'sel': '#FFDE46', 'sel_hi': '#F5D02A', 'seltext': '#463400', 'light': '#FFF4C4', 'card': '#FFFFFF', 'ok': '#0F6E56', 'bad': '#A32D2D',
     'badbg': '#FCEBEB', 'warn': '#854F0B', 'log': '#403C60', 'logtext': '#EEE9FD'}
FAMILY = 'Segoe UI' if WIN else 'Helvetica Neue' if MAC else 'DejaVu Sans'


def F(size, bold=False):   # Tk on macOS counts a point as one pixel, Windows as 4/3: the same size on screen
    return (FAMILY, round(size * 1.3) if MAC else size, 'bold' if bold else 'normal')


def rrect(cv, x0, y0, x1, y1, r, **kw):
    r = max(1, min(r, (x1 - x0) / 2, (y1 - y0) / 2))
    pts = (x0 + r, y0, x1 - r, y0, x1, y0, x1, y0 + r, x1, y1 - r, x1, y1, x1 - r, y1, x0 + r, y1, x0, y1, x0, y1 - r, x0, y0 + r, x0, y0)
    return cv.create_polygon(pts, smooth=True, **kw)


def label(parent, text='', size=11, bold=False, fg=None, wrap=True, **kw):
    """A label on its parent's colour; wrap=True folds it to the width it is packed to"""
    lb = tk.Label(parent, text=text, font=F(size, bold), fg=fg or C['text'], bg=parent['bg'], justify='left', anchor='w', **kw)
    if wrap: lb.bind('<Configure>', lambda e: lb.configure(wraplength=max(60, e.width - 4)))
    return lb


class Round(tk.Canvas):
    """A rounded panel: widgets go in .body. It takes the width it is packed to and the height its body asks for"""
    def __init__(self, parent, fill=None, edge=None, pad=12, radius=16, command=None):
        super().__init__(parent, bg=parent['bg'], highlightthickness=0, bd=0, height=2 * pad)
        self.fill, self.edge, self.pad, self.radius, self.command = fill or C['card'], edge, pad, radius, command
        self.body = tk.Frame(self, bg=self.fill)
        self.item = self.create_window(pad, pad, window=self.body, anchor='nw')
        self.body.bind('<Configure>', self.fit); self.bind('<Configure>', self.fit)

    def fit(self, _=None):
        w = self.winfo_width()
        if w > 2 * self.pad: self.itemconfigure(self.item, width=w - 2 * self.pad)
        h = self.body.winfo_reqheight() + 2 * self.pad
        if int(float(self.cget('height'))) != h: self.configure(height=h)
        self.delete('bg'); rrect(self, 1, 1, w - 2, h - 2, self.radius, fill=self.fill, outline=self.edge or self.fill, width=2, tags='bg')
        self.tag_lower('bg')

    def clickable(self):   # once the body is filled: a click anywhere on the panel is its command
        def walk(w):
            w.bind('<Button-1>', lambda e: self.command()); w.configure(cursor='hand2')
            for c in w.winfo_children(): walk(c)
        if self.command: walk(self)


class Pill(tk.Canvas):
    """A rounded button: kind 'primary' (yellow, the one thing to do next) or 'plain' (lavender)"""
    def __init__(self, parent, text, command, kind='plain', size=11):
        self.font = tkfont.Font(font=F(size, kind == 'primary'))
        w, h = self.font.measure(text) + 40, self.font.metrics('linespace') + 16
        super().__init__(parent, width=w, height=h, bg=parent['bg'], highlightthickness=0, bd=0, cursor='hand2')
        self.text, self.command, self.kind, self.enabled, self.hover = text, command, kind, True, False
        self.bind('<Enter>', lambda e: self.set_hover(True)); self.bind('<Leave>', lambda e: self.set_hover(False))
        self.bind('<Button-1>', lambda e: self.enabled and self.command and self.command())
        self.draw()

    def set_hover(self, on): self.hover = on; self.draw()

    def set_enabled(self, on): self.enabled = on; self.configure(cursor='hand2' if on else 'arrow'); self.draw()

    def draw(self):
        if not self.enabled: fill, fg = C['pill'], C['edge']
        elif self.kind == 'primary': fill, fg = C['sel_hi'] if self.hover else C['sel'], C['seltext']
        else: fill, fg = C['pill_hi'] if self.hover else C['pill'], C['title']
        w, h = int(self.cget('width')), int(self.cget('height'))
        self.delete('all'); rrect(self, 0, 0, w - 1, h - 1, h / 2, fill=fill, outline=fill)
        self.create_text(w / 2, h / 2, text=self.text, font=self.font, fill=fg)


class Tag(tk.Canvas):   # a small pill that is not a button: the steps along the top
    def __init__(self, parent, text, fill, fg):
        font = tkfont.Font(font=F(9))
        w, h = font.measure(text) + 22, font.metrics('linespace') + 8
        super().__init__(parent, width=w, height=h, bg=parent['bg'], highlightthickness=0, bd=0)
        rrect(self, 0, 0, w - 1, h - 1, h / 2, fill=fill, outline=fill); self.create_text(w / 2, h / 2, text=text, font=font, fill=fg)


STEPS = ('Start', 'What to make', f'Your {PC}', 'Game files', 'Build', 'Play')
# Each line of a build that starts a stage of the walkthrough's list (nxs2.py's say() lines, the [m:ss] taken off)
BUILD_STAGES = (('Copying your game files', r'^(RomFS image|\.nup: )', ''),
                ("Preparing the game's code", r'^(rewriting |rewrite: |building the Mac host|host: |analysing the game|analysis: |reading the executable)', ''),
                ('Preparing the graphics', r'^(extracting the shaders|corpus: |adding shader dumps|lowering the shaders|packaging the shaders|tables: |'
                                           r'translating the shaders|Metal libraries|shaders: )',
                 'The longest part on a Mac: often 30 to 45 minutes.' if MAC else ''),
                (f'Recompiling the game for your {PC}', r'^(recompiling the game|game\.dll: )', 'The longest part: often 15 to 40 minutes.'),
                ('Packing it up', r'^(packing the Mac app|building the IPA|preparing the Xcode|Windows game: )', ''))
REFRESH_STAGES = (('Finding what you played', r'^shaders: ', ''),
                  ('Preparing the new graphics', r'^(extracting the shaders|corpus: |adding shader dumps|lowering the shaders|packaging the shaders|tables: |'
                                                 r'translating the shaders|Metal libraries)', 'Usually a few minutes.'),
                  ('Packing it up', r'^(packing the Mac app|building the IPA|preparing the Xcode|the Xcode project is refreshed)', ''))
# The tool checks in plain words (nxs2.py doctor's names); None: not shown (the window itself proves it)
CHECK_NAMES = ((r'^Windows on x64', 'Windows (64-bit)'), (r'^Python 3', 'Python'), (r'^(python module|capstone)', 'Python add-ons'), (r'^Tk ', None),
               (r'^\.NET', '.NET 10 (it prepares the graphics)'), (r'^git$', 'Git'), (r'^a standard font', 'A standard font'),
               (r'^a CPU with AVX2', 'Your processor'), (r'^Vulkan', 'Graphics driver (Vulkan)'), (r'^the Windows game tools', 'The Windows game tools'),
               (r'^macOS on Apple silicon', 'An Apple silicon Mac'), (r'^(Xcode command line tools|clang$|make$)', 'Apple developer tools'),
               (r'^Metal compiler for macosx', 'Metal (the Mac graphics)'), (r'^Metal compiler for iphoneos', 'Metal (the iPhone graphics)'),
               (r'^xcodegen', 'XcodeGen (the iPhone app)'), (r'^LLVM|^Metal Developer Tools|^ldid|^rcodesign', 'The iPhone app tools'))


def fix_text(hints):
    """One card's fix from its checks' hints: pip installs merged into one command, the same hint once (a Mac's own environment fixes every
    missing module). Returns (what the card shows, what Copy copies: only the commands after "in Terminal: ")."""
    pips, other = [], []
    for h in hints:
        m = re.match(r'^(.+?) -m pip install (".+")$', h)
        if m: pips.append(m)
        elif h not in other: other.append(h)
    lines = ([f'{pips[0].group(1)} -m pip install ' + ' '.join(m.group(2) for m in pips)] if pips else []) + other
    fix = '\n'.join(lines)
    if any(' -m venv ' in l for l in other): fix += '\nThen quit the builder (⌘Q) and open it again: it uses that environment by itself.'
    return fix, '\n'.join(l.split('in Terminal: ', 1)[1] if 'in Terminal: ' in l else l for l in lines)


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f'Pokopia Native Builder {tool_version()}')
        self.configure(bg=C['bg']); self.minsize(820, 600); self.geometry('900x760')
        self.proc = None; self.q = queue.Queue(); self.t0 = None; self.running = None; self.last_stage = ''; self.result = {}
        self.wiz = None; self.lines = []; self.wlog = None; self.details = False; self.dump_info = None
        self.checks = []; self.prog = None; self.bar = None; self.space_ok = True
        self.settings = self.load_settings()
        self.adv = tk.Frame(self); self.build_ui(self.adv)   # Advanced: the one-screen view; its variables are the walkthrough's too
        self.walk = tk.Frame(self, bg=C['bg']); self.build_walk()
        self.walk.pack(fill='both', expand=True)
        self.after(100, self.poll)
        self.check_setup()
        self.home()

    # ---------------------------------------------------------------- settings
    def load_settings(self):
        try: return json.loads(SETTINGS.read_text())
        except Exception: return {}

    def save_settings(self, **more):   # the walkthrough's own keys (built, built_with, refresh_due) are kept alongside
        s = dict(self.settings)
        s.update({'dump': self.dump.get(), 'out': self.out.get(), 'mac_app': self.mac_app.get(), 'ipa': self.ipa.get(), 'xcode': self.xcode.get(),
                  'team': self.team.get(), 'no_ios': self.no_ios.get(), 'windows': self.windows.get()}, **more)
        self.settings = s
        try: SETTINGS.write_text(json.dumps(s))
        except Exception: pass

    # ---------------------------------------------------------------- Advanced: the one-screen view
    def build_ui(self, parent):
        s = self.settings
        pad = {'padx': 8, 'pady': 4}
        back = ttk.Label(parent, text='← Back to the walkthrough', foreground='#2f6fde', cursor='hand2', font=('', 10, 'underline'))
        back.pack(anchor='w', padx=18, pady=(10, 0)); back.bind('<Button-1>', lambda e: self.show_walk())
        f = ttk.Frame(parent); f.pack(fill='both', expand=True, padx=10, pady=10)

        ttk.Label(f, text='1. Your game dump', font=('', 12, 'bold')).grid(row=0, column=0, sticky='w', **pad)
        help_link = ttk.Label(f, text='Need help? Ask on Discord', foreground='#2f6fde', cursor='hand2', font=('', 10, 'underline'))
        help_link.grid(row=0, column=2, sticky='e', **pad); help_link.bind('<Button-1>', lambda e: webbrowser.open(DISCORD))
        self.dump = tk.StringVar(value=s.get('dump', ''))
        ttk.Entry(f, textvariable=self.dump, width=70).grid(row=1, column=0, columnspan=2, sticky='ew', **pad)
        db = ttk.Frame(f); db.grid(row=1, column=2, sticky='w', **pad)
        ttk.Button(db, text='Choose…', command=self.choose_dump).pack(side='left')
        ttk.Button(db, text='.nup file…', command=self.choose_nup).pack(side='left', padx=(6, 0))
        ttk.Label(f, text='The folder your dumping tool made from your own copy: it holds updates/655360 (game version 2.0.1) or updates/327680 '
                          '(1.0.5) with exefs and romfs inside, or exefs and romfs directly. A .nup file works too. Your dump is only read, never changed.',
                  foreground='#666', wraplength=720, justify='left').grid(row=2, column=0, columnspan=3, sticky='w', padx=8)

        ttk.Label(f, text='2. Build folder', font=('', 12, 'bold')).grid(row=3, column=0, sticky='w', **pad)
        self.out = tk.StringVar(value=s.get('out', ''))
        ttk.Entry(f, textvariable=self.out, width=70).grid(row=4, column=0, columnspan=2, sticky='ew', **pad)
        ttk.Button(f, text='Change…', command=self.choose_out).grid(row=4, column=2, sticky='w', **pad)
        ttk.Label(f, text='Where everything made from your game goes (about 12 GB per game version). Leave it empty for a folder next to your dump.' +
                          (' Pick one outside Documents, Desktop and Downloads: Windows Security can block writing there.' if WIN else ''),
                  foreground='#666', wraplength=720, justify='left').grid(row=5, column=0, columnspan=3, sticky='w', padx=8)

        ttk.Label(f, text='3. What to make', font=('', 12, 'bold')).grid(row=6, column=0, sticky='w', **pad)
        o = ttk.Frame(f); o.grid(row=7, column=0, columnspan=3, sticky='w', padx=8)
        self.mac_app = tk.BooleanVar(value=s.get('mac_app', MAC)); self.ipa = tk.BooleanVar(value=s.get('ipa', not WIN))
        self.windows = tk.BooleanVar(value=s.get('windows', WIN))
        self.xcode = tk.BooleanVar(value=s.get('xcode', False)); self.team = tk.StringVar(value=s.get('team', ''))
        self.no_ios = tk.BooleanVar(value=s.get('no_ios', False))
        self.full_speed = tk.BooleanVar(value=False)   # never remembered: every session starts gentle
        if MAC: ttk.Checkbutton(o, text='Mac app', variable=self.mac_app).pack(side='left', padx=(0, 14))
        if WIN: ttk.Checkbutton(o, text='Windows: play on this PC', variable=self.windows).pack(side='left', padx=(0, 14))
        ttk.Checkbutton(o, text='iPhone IPA (the iPhone app, unsigned)', variable=self.ipa).pack(side='left', padx=(0, 14))
        if MAC:
            ttk.Checkbutton(o, text='Xcode project, team ID:', variable=self.xcode).pack(side='left')
            ttk.Entry(o, textvariable=self.team, width=12).pack(side='left', padx=(4, 14))
            ttk.Checkbutton(o, text='Mac only (skip the iPhone shader libraries, faster)', variable=self.no_ios).pack(side='left')

        sp = ttk.Frame(f); sp.grid(row=8, column=0, columnspan=3, sticky='w', padx=8, pady=(6, 0))
        ttk.Checkbutton(sp, text='Full speed (every CPU thread)', variable=self.full_speed).pack(side='left')
        ttk.Label(sp, text='  Off: a few cores at low priority, slower but the computer stays cool and usable.' if not MAC else
                  '  Off: all cores at low priority. On: the same at normal priority.', foreground='#666').pack(side='left')

        b = ttk.Frame(f); b.grid(row=9, column=0, columnspan=3, sticky='ew', padx=8, pady=(10, 4))
        self.buttons = {}
        for key, text, cmd in (('doctor', 'Check tools', self.do_doctor), ('setup', 'Setup', self.do_setup), ('build', 'Build', self.do_build),
                               ('refresh', 'Refresh shaders', self.do_refresh), ('sign', 'Sign IPA', self.do_sign)) + \
                              ((('play', 'Play', self.do_play), ('import', 'Import save…', self.do_import)) if WIN else ()):
            self.buttons[key] = ttk.Button(b, text=text, command=cmd); self.buttons[key].pack(side='left', padx=(0, 6))
        self.stop_btn = ttk.Button(b, text='Stop', command=self.do_stop, state='disabled'); self.stop_btn.pack(side='left', padx=(0, 6))
        ttk.Button(b, text='Open build folder', command=self.open_out).pack(side='right')
        steps = ('Setup once for each new download, then Build (the first build takes a while; stopping is safe, Build again carries on). '
                 'Then Play' + (', or Sign IPA' if WIN else '') + ' for the iPhone app. Import save brings your save from Eden, the iPhone or the Mac. '
                 'Refresh shaders finishes the iPhone app after a first play; the README explains.' if WIN else
                 'Setup once for each new download, then Build (the first build takes a while; stopping is safe, Build again carries on). '
                 'Sign IPA makes the iPhone app installable; Refresh shaders finishes it after a first play. The README explains each step.')
        ttk.Label(f, text=steps, foreground='#666', wraplength=720, justify='left').grid(row=10, column=0, columnspan=3, sticky='w', padx=8)

        self.status = tk.StringVar(value='')
        ttk.Label(f, textvariable=self.status, font=('', 11)).grid(row=11, column=0, columnspan=3, sticky='w', padx=8, pady=(6, 2))
        self.log = tk.Text(f, height=18, wrap='none', font=('Menlo' if MAC else 'Consolas', 10), state='disabled')
        self.log.grid(row=12, column=0, columnspan=3, sticky='nsew', padx=8, pady=(0, 4))
        sb = ttk.Scrollbar(f, orient='vertical', command=self.log.yview); sb.grid(row=12, column=3, sticky='ns'); self.log['yscrollcommand'] = sb.set
        f.columnconfigure(0, weight=1); f.rowconfigure(12, weight=1)
        self.protocol('WM_DELETE_WINDOW', self.on_close)

    # ---------------------------------------------------------------- helpers
    def choose_dump(self):
        d = filedialog.askdirectory(title='Your dump folder', initialdir=self.dump.get() or str(pathlib.Path.home()))
        if d: self.dump.set(d); self.save_settings()

    def choose_nup(self):
        d = filedialog.askopenfilename(title='Your dump as a .nup file', filetypes=[('NUP container', '*.nup'), ('All files', '*.*')],
                                       initialdir=str(pathlib.Path(self.dump.get()).parent) if self.dump.get() else str(pathlib.Path.home()))
        if d: self.dump.set(d); self.save_settings()

    def choose_out(self):
        d = filedialog.askdirectory(title='Build folder', initialdir=self.out.get() or str(pathlib.Path.home()))
        if d: self.out.set(d); self.save_settings()

    def build_dir(self):
        if self.out.get().strip(): return pathlib.Path(self.out.get().strip()).expanduser()
        return build_dir_for(self.dump.get().strip()) if self.dump.get().strip() else None

    def open_out(self):
        d = self.build_dir()
        if not d or not d.exists(): return messagebox.showinfo('Build folder', 'No build folder yet: choose your dump and press Build.')
        if MAC: subprocess.Popen(['open', str(d)])
        elif WIN: os.startfile(str(d))
        else: subprocess.Popen(['xdg-open', str(d)])

    def write(self, line):
        self.log['state'] = 'normal'; self.log.insert('end', line + '\n'); self.log.see('end'); self.log['state'] = 'disabled'

    def check_setup(self):
        nx = pathlib.Path(os.environ.get('NXNATIVE') or HERE / 'nxnative')
        if not (nx / 'dm' / 'host' / 'Makefile').is_file():
            self.status.set('First time here: press Setup (fetches nxnative and builds the shader tool, about a minute).' +
                            (' Untick "iPhone IPA" first if you only want the Windows game: Setup then needs no Apple tools.' if WIN else ''))
        else:
            self.status.set('Ready. Choose your dump folder and press Build.')

    # ---------------------------------------------------------------- running nxs2.py
    def start(self, what, args, secret=None):   # secret: one line handed to the command on its stdin (the .p12 password), never shown or logged
        if self.proc: return
        py = pathlib.Path(sys.executable)
        if WIN and py.name.lower() == 'pythonw.exe' and py.with_name('python.exe').is_file(): py = py.with_name('python.exe')
        cmd = [str(py), '-u', str(NXS2)] + [str(a) for a in args]
        self.write('$ ' + ' '.join(cmd[2:]))
        try:
            kw = {}
            # Windows: the build gets a console of its own, hidden. Every tool it starts shares that console. Without one (pythonw, as this window
            # runs), each console tool would open a window of its own: the shader stage starts thousands
            if WIN: kw['creationflags'] = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW
            else: kw['start_new_session'] = True
            self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.PIPE if secret is not None else subprocess.DEVNULL,
                                         text=True, errors='replace', cwd=str(HERE), **kw)
            if secret is not None: self.proc.stdin.write(secret + '\n'); self.proc.stdin.close()
        except OSError as e:
            return messagebox.showerror('Cannot start', f'{e}')
        self.running = what; self.t0 = time.time(); self.result = {}
        for k in self.buttons.values(): k['state'] = 'disabled'
        self.stop_btn['state'] = 'normal'
        threading.Thread(target=self.pump, daemon=True).start()

    def pump(self):
        for line in self.proc.stdout: self.q.put(line.rstrip('\n'))
        self.q.put(('exit', self.proc.wait()))

    def poll(self):
        try:
            while True:
                item = self.q.get_nowait()
                if isinstance(item, tuple): self.finished(item[1])
                else: self.on_line(item)
        except queue.Empty: pass
        if self.proc and self.t0: self.status.set(self.status_text())
        if self.wiz and self.wiz.get('tick'): self.wiz['tick']()
        self.after(150, self.poll)

    def status_text(self):
        el = int(time.time() - self.t0); el = f'{el // 60}:{el % 60:02d}'
        return f'{self.running}… {el}   {self.last_stage}'

    def on_line(self, line):
        self.write(line)
        self.lines = self.lines[-400:] + [line]
        if self.wlog: self.wlog_add(line)
        if self.wiz and self.wiz.get('line'): self.wiz['line'](line)
        m = re.match(r'^\[\d+:\d\d(?::\d\d)?\] (.*)', line)
        if m: self.last_stage = m.group(1)[:90]
        for key, pat in (('app', r'Mac app: (.*)'), ('ipa', r'IPA: (.*?) \('), ('xcode', r'Xcode project: (.*)'), ('signed', r'signed IPA: (.*?)  '),
                         ('windows', r'Windows game: (.*?)  \(')):
            m = re.search(pat, line)
            if m: self.result[key] = m.group(1)
        if line.startswith('[FAIL]'): self.result['fail'] = line[7:]

    def finished(self, rc):
        self.proc = None
        for k in self.buttons.values(): k['state'] = 'normal'
        self.stop_btn['state'] = 'disabled'
        el = int(time.time() - self.t0); el = f'{el // 60}:{el % 60:02d}'
        if self.wiz:   # started by the walkthrough: its page says what happened, no dialog
            done, self.wiz = self.wiz.get('done'), None; self.t0 = None
            self.status.set(f'{self.running} {"done" if rc == 0 else "stopped"} after {el}.')
            if done: done(rc)
            return
        if rc == 0:
            made = ', '.join(v for k, v in self.result.items() if k != 'fail')
            self.status.set(f'{self.running} done in {el}.' + (f'  {made}' if made else ''))
            if self.running == 'Build' and made:
                nxt = ' '.join(t for k, t in (('windows', 'Press Play to start the game.'), ('app', 'The Mac app is in the build folder.'),
                                              ('ipa', 'Sign IPA makes the iPhone app installable; the README says how to get it onto the phone.')) if k in self.result)
                messagebox.showinfo('Done', f'Build finished in {el}.\n\n{chr(10).join(self.result.values())}\n\n{nxt or "Open the build folder for the outputs."}')
        elif rc in (130, -15, -2, 143, 1) and self.result.get('stopped'):
            self.status.set(f'{self.running} stopped after {el}. Press the button again to continue where it left off.')
        else:
            self.status.set(f'{self.running} failed after {el}: {self.result.get("fail", f"exit {rc}")}')
            messagebox.showerror(f'{self.running} failed', self.result.get('fail', f'exit code {rc}') + '\n\nThe full output of every stage is in <build folder>/logs/.'
                                 f'\nStuck? Ask in the Discord ({DISCORD}) or check "Common problems" in the README.')
        self.t0 = None

    def do_stop(self):
        if not self.proc: return
        self.result['stopped'] = True
        try:
            if WIN: subprocess.run(['taskkill', '/T', '/F', '/PID', str(self.proc.pid)], capture_output=True)
            else: os.killpg(os.getpgid(self.proc.pid), 15)
        except Exception: pass

    def on_close(self):
        if self.proc and not messagebox.askyesno('A build is running', 'Stop the build and close? (It can be continued later.)'): return
        self.do_stop(); self.save_settings(); self.destroy()

    # ---------------------------------------------------------------- the buttons
    def need_dump(self):
        d = self.dump.get().strip()
        p = pathlib.Path(d).expanduser() if d else None
        if not p or not (p.is_dir() or (p.is_file() and p.suffix.lower() == '.nup')):
            messagebox.showwarning('Dump folder', 'Choose your dump folder (or your .nup file) first.'); return None
        return d

    def common(self):
        return ['--out', self.out.get().strip()] if self.out.get().strip() else []

    def skip_ios(self):   # Windows: no iPhone toolchain unless the IPA is ticked; Mac: the "Mac only" box
        return (WIN and not self.ipa.get()) or (MAC and self.no_ios.get())

    def do_doctor(self):
        self.last_stage = ''
        self.start('Check tools', ['doctor'] + (['--no-ios'] if self.skip_ios() else []) + (['--no-windows'] if WIN and not self.windows.get() else []))

    def do_setup(self):
        self.last_stage = ''; self.start('Setup', ['setup'] + (['--no-ios'] if self.skip_ios() else []))

    def do_build(self):
        d = self.need_dump()
        if not d: return
        self.save_settings()
        args = ['build', d] + self.common()
        if MAC and self.mac_app.get(): args.append('--mac-app')
        if WIN and self.windows.get(): args.append('--windows')
        if self.ipa.get(): args.append('--ipa')
        if MAC and self.xcode.get():
            if not re.fullmatch(r'[A-Z0-9]{10}', self.team.get().strip()): return messagebox.showwarning('Team ID', 'The Xcode project needs your Apple developer team ID (10 characters).')
            args += ['--xcode', '--team-id', self.team.get().strip()]
        if MAC and self.no_ios.get(): args.append('--no-ios')
        if self.full_speed.get(): args.append('--full-speed')
        if not any(a in args for a in ('--mac-app', '--ipa', '--xcode', '--windows')): return messagebox.showwarning('Outputs', 'Tick at least one output.')
        self.last_stage = 'starting'; self.start('Build', args)

    def do_refresh(self):
        b = self.build_dir()
        if not b or not b.is_dir(): return messagebox.showwarning('Refresh', 'No build folder yet: build first. Refresh adds the shaders recorded while playing.')
        args = ['refresh', str(b)]
        if MAC and self.xcode.get() and self.team.get().strip(): args += ['--team-id', self.team.get().strip()]
        if self.full_speed.get(): args.append('--full-speed')
        self.last_stage = 'starting'; self.start('Refresh', args)

    def do_sign(self):
        b = self.build_dir()
        unsigned = b / 'out' / 'PokemonPokopia-unsigned.ipa' if b else None
        if WIN or (MAC and not (b and (b / 'xcode' / 'DmIOS.xcodeproj').is_dir()) and unsigned and unsigned.is_file()):
            return self.do_sign_certificate(b, unsigned)
        if not b or not (b / 'xcode' / 'DmIOS.xcodeproj').is_dir(): return messagebox.showwarning('Sign', 'No Xcode project yet: tick "Xcode project" with your team ID and build first.')
        if not re.fullmatch(r'[A-Z0-9]{10}', self.team.get().strip()): return messagebox.showwarning('Team ID', 'Enter your Apple developer team ID (10 characters).')
        self.last_stage = 'starting'; self.start('Sign', ['sign', str(b), '--team-id', self.team.get().strip()])

    def do_sign_certificate(self, b, unsigned):   # without Xcode: your certificate (.p12) and development profile (.mobileprovision); docs/WINDOWS.md
        if not unsigned or not unsigned.is_file(): return messagebox.showwarning('Sign', 'No unsigned IPA yet: tick "iPhone IPA" and build first.')
        from tkinter import simpledialog
        p12 = filedialog.askopenfilename(title='Your Apple Development certificate (.p12)', filetypes=[('Certificate', '*.p12 *.pfx'), ('All files', '*.*')])
        if not p12: return
        prof = filedialog.askopenfilename(title='Your development provisioning profile (.mobileprovision)', filetypes=[('Provisioning profile', '*.mobileprovision'), ('All files', '*.*')])
        if not prof: return
        pw = simpledialog.askstring('Certificate password', f'Password of {pathlib.Path(p12).name}:', show='*', parent=self)
        if pw is None: return
        self.last_stage = 'starting'; self.start('Sign', ['sign', str(b), '--p12', p12, '--profile', prof, '--password-stdin'], secret=pw)


    def win_launcher(self):
        b = self.build_dir(); out = b / 'out' if b else None
        hits = sorted(out.glob('Play Pokopia *.cmd')) if out and out.is_dir() else []
        return hits[-1] if hits else None

    def do_play(self):   # the launcher in its own console window: this window stays free
        cmd = self.win_launcher()
        if not cmd: return messagebox.showwarning('Play', 'No Windows game yet: tick "Windows: play on this PC" and press Build first.')
        import nxs2; nxs2.dotnet_on_path()   # .NET installed since this window opened: the game translates run-time shaders with it
        subprocess.Popen(['cmd', '/c', str(cmd)], creationflags=subprocess.CREATE_NEW_CONSOLE, cwd=str(cmd.parent))
        self.status.set(f'Started {cmd.name}. Its console window shows the controls and where the log is.')

    def do_import(self):
        b = self.build_dir()
        if not b or not (b / 'windows' / 'play.json').is_file(): return messagebox.showwarning('Import save', 'Build the Windows game first: the save goes beside it.')
        eden = messagebox.askyesnocancel('Import save', "Take the save from Eden on this PC?\n\nYes: Eden's own save of Pokopia (read, never changed).\n"
                                         'No: choose a folder (one holding B00, G00 and S00: a phone or Mac save, or a copy).\n\n'
                                         'The save the Windows game has now is kept beside it, not deleted.')
        if eden is None: return
        if eden: args = ['import-save', str(b), '--eden']
        else:
            d = filedialog.askdirectory(title='The save folder (B00, G00, S00 inside)')
            if not d: return
            args = ['import-save', str(b), d]
        self.last_stage = 'starting'; self.start('Import save', args)

    # ---------------------------------------------------------------- the walkthrough: frame, pages, pieces
    def build_walk(self):
        w = self.walk
        top = tk.Frame(w, bg=C['bg']); top.pack(fill='x', padx=28, pady=(18, 0))
        label(top, 'Pokopia Native', 20, True, C['title'], wrap=False).pack(side='left')
        label(top, f'builder {tool_version()}', 10, fg=C['soft'], wrap=False).pack(side='left', padx=(10, 0), pady=(10, 0))
        Pill(top, 'Advanced', self.show_adv, size=10).pack(side='right')
        self.dots = tk.Frame(w, bg=C['bg']); self.dots.pack(fill='x', padx=28, pady=(10, 0))
        tk.Frame(w, bg=C['pill_hi'], height=2).pack(fill='x', padx=28, pady=(12, 0))
        foot = tk.Frame(w, bg=C['bg']); foot.pack(side='bottom', fill='x', padx=28, pady=(8, 18))
        ask = label(foot, 'Stuck? Ask in the Discord', 10, fg=C['title'], wrap=False, cursor='hand2')
        ask.configure(font=(FAMILY, F(10)[1], 'underline')); ask.bind('<Button-1>', lambda e: webbrowser.open(DISCORD)); ask.pack(side='left')
        self.nav = tk.Frame(foot, bg=C['bg']); self.nav.pack(side='right')
        # the page scrolls when it is longer than the window (a scroll bar shows only then)
        hold = tk.Frame(w, bg=C['bg']); hold.pack(fill='both', expand=True, padx=(28, 14), pady=(14, 0))
        self.cv = tk.Canvas(hold, bg=C['bg'], highlightthickness=0, bd=0)
        self.sb = ttk.Scrollbar(hold, orient='vertical', command=self.cv.yview); self.cv.configure(yscrollcommand=self.sb.set)
        self.cv.pack(side='left', fill='both', expand=True, padx=(0, 14))
        self.pg = tk.Frame(self.cv, bg=C['bg']); self.pg_item = self.cv.create_window(0, 0, window=self.pg, anchor='nw')
        self.cv.bind('<Configure>', lambda e: (self.cv.itemconfigure(self.pg_item, width=e.width), self.fit_scroll()))
        self.pg.bind('<Configure>', lambda e: self.fit_scroll())
        for ev in ('<MouseWheel>', '<Button-4>', '<Button-5>'): self.bind_all(ev, self.wheel, add='+')

    def fit_scroll(self):
        h, view = self.pg.winfo_reqheight(), self.cv.winfo_height()
        self.cv.configure(scrollregion=(0, 0, self.cv.winfo_width(), max(h, view)))
        if h > view + 4 and not self.sb.winfo_ismapped(): self.sb.pack(side='right', fill='y')
        elif h < view - 20 and self.sb.winfo_ismapped(): self.sb.pack_forget(); self.cv.yview_moveto(0)

    def wheel(self, e):
        if not self.walk.winfo_ismapped() or not self.sb.winfo_ismapped() or isinstance(e.widget, tk.Text): return
        self.cv.yview_scroll(-2 if getattr(e, 'delta', 0) > 0 or getattr(e, 'num', 0) == 4 else 2, 'units')

    def show_adv(self):
        self.walk.pack_forget(); self.adv.pack(fill='both', expand=True)

    def show_walk(self):
        self.adv.pack_forget(); self.walk.pack(fill='both', expand=True)
        if not self.proc: self.home()

    def page(self, step, title, intro=''):
        """Empties the page and its buttons, marks the step along the top (None: no step) and returns the page to fill"""
        self.wlog = self.bar = None
        for c in self.pg.winfo_children() + self.nav.winfo_children() + self.dots.winfo_children(): c.destroy()
        self.cv.yview_moveto(0)
        for i, n in enumerate(STEPS if step is not None else ()):   # pages outside the first walk through (welcome back, refresh) have no steps
            fill, fg = (C['pill'], C['soft']) if i > step else (C['sel'], C['seltext']) if i == step else (C['pill_hi'], C['title'])
            Tag(self.dots, ('✓ ' if i < step else '') + n, fill, fg).pack(side='left', padx=(0, 6))
        label(self.pg, title, 18, True, C['title']).pack(fill='x')
        if intro: label(self.pg, intro, 11).pack(fill='x', pady=(4, 8))
        return self.pg

    def navb(self, back=None, nxt=None, text='Next', on=True):
        if back: Pill(self.nav, 'Back', back).pack(side='left', padx=(0, 8))
        if nxt:
            b = Pill(self.nav, text, nxt, 'primary'); b.pack(side='left'); b.set_enabled(on)
            return b

    def card(self, parent, fill=None, edge=None, command=None, pady=4):
        r = Round(parent, fill=fill, edge=edge, command=command); r.pack(fill='x', pady=pady)
        return r

    def choice(self, parent, title, text, on, command, tag=''):   # a card you tick by clicking it
        r = self.card(parent, C['light'] if on else C['card'], C['sel'] if on else C['pill'], command)
        row = tk.Frame(r.body, bg=r.fill); row.pack(fill='x')
        dot = tk.Canvas(row, width=26, height=26, bg=r.fill, highlightthickness=0); dot.pack(side='left', anchor='n', padx=(0, 12), pady=2)
        dot.create_oval(2, 2, 24, 24, fill=C['sel'] if on else C['card'], outline=C['seltext'] if on else C['edge'], width=2)
        if on: dot.create_text(13, 13, text='✓', font=F(10, True), fill=C['seltext'])
        col = tk.Frame(row, bg=r.fill); col.pack(side='left', fill='x', expand=True)
        head = tk.Frame(col, bg=r.fill); head.pack(fill='x')
        label(head, title, 12, True, C['title'], wrap=False).pack(side='left')
        if tag: label(head, tag, 10, fg=C['soft'], wrap=False).pack(side='left', padx=(8, 0))
        label(col, text, 10, fg=C['soft']).pack(fill='x')
        r.clickable()
        return r

    def step_card(self, parent, n, title, text, buttons=()):   # a numbered step with its buttons on the right
        r = self.card(parent)
        row = tk.Frame(r.body, bg=r.fill); row.pack(fill='x')
        num = tk.Canvas(row, width=26, height=26, bg=r.fill, highlightthickness=0); num.pack(side='left', anchor='n', padx=(0, 12), pady=2)
        num.create_oval(1, 1, 25, 25, fill=C['title'], outline=C['title']); num.create_text(13, 13, text=str(n), font=F(10, True), fill='#FFFFFF')
        side = tk.Frame(row, bg=r.fill); side.pack(side='right', anchor='n', padx=(12, 0))
        for text_, cmd, kind in buttons: Pill(side, text_, cmd, kind).pack(anchor='e', pady=(0, 6))
        col = tk.Frame(row, bg=r.fill); col.pack(side='left', fill='x', expand=True)
        label(col, title, 12, True, C['title']).pack(fill='x'); label(col, text, 10, fg=C['soft']).pack(fill='x')
        return r

    def row(self, parent, name, state, detail='', hint='', copy=None):
        """One line of a checklist, state ok, bad, wait, warn or info, with a hint below it"""
        fill = C['badbg'] if state == 'bad' else C['card']
        r = Round(parent, fill=fill, edge=C['bad'] if state == 'bad' else None, pad=10, radius=12); r.pack(fill='x', pady=3)
        top = tk.Frame(r.body, bg=fill); top.pack(fill='x')
        label(top, name, 11, wrap=False).pack(side='left')
        text, fg = {'ok': ('✓ ' + (detail or 'ready'), C['ok']), 'bad': ('✗ ' + (detail or 'missing'), C['bad']), 'wait': (detail or 'checking…', C['soft']),
                    'warn': ('! ' + (detail or 'heads-up'), C['warn']), 'info': (detail, C['text'])}[state]
        tk.Label(top, text=text, font=F(11, state in ('ok', 'bad')), fg=fg, bg=fill).pack(side='right')
        if hint or copy:
            low = tk.Frame(r.body, bg=fill); low.pack(fill='x', pady=(4, 0))
            if copy: Pill(low, 'Copy', lambda: self.copy(copy), size=9).pack(side='right', anchor='n', padx=(10, 0))
            label(low, hint, 10, fg=C['bad'] if state == 'bad' else C['warn'] if state == 'warn' else C['soft']).pack(side='left', fill='x', expand=True)
        return r

    def copy(self, text):
        self.clipboard_clear(); self.clipboard_append(text); self.status.set('Copied.')

    def run_wiz(self, start, line=None, done=None, tick=None):   # start: a do_* method; its run reports to the walkthrough, not to a dialog
        if self.proc: return False
        self.wiz = {'line': line, 'done': done, 'tick': tick}
        start()
        if not self.proc: self.wiz = None
        return bool(self.proc)

    @staticmethod
    def made_text(made):
        names = {'windows': 'the game for this PC', 'app': 'the Mac app', 'ipa': 'the iPhone app', 'signed': 'the iPhone app', 'xcode': 'the iPhone app'}
        out = list(dict.fromkeys(names[k] for k in made if k in names))
        return ' and '.join(out) if out else 'nothing yet'

    def details_area(self, pg):   # the raw output, one click away
        holder = tk.Frame(pg, bg=C['bg']); holder.pack(fill='both', expand=True, pady=(8, 0))
        def draw():
            for c in holder.winfo_children(): c.destroy()
            self.wlog = None
            Pill(holder, 'Hide details' if self.details else 'Show details', toggle, size=10).pack(anchor='w')
            if self.details:
                t = tk.Text(holder, height=10, wrap='none', font=('Consolas' if WIN else 'Menlo' if MAC else 'DejaVu Sans Mono', F(9)[1]), bg=C['log'],
                            fg=C['logtext'], relief='flat', bd=0, padx=8, pady=6)
                t.pack(fill='both', expand=True, pady=(6, 0)); t.insert('end', '\n'.join(self.lines[-300:]) + '\n'); t.see('end'); t.configure(state='disabled')
                self.wlog = t
        def toggle(): self.details = not self.details; draw()
        draw()

    def wlog_add(self, line):
        t = self.wlog
        if not t.winfo_exists(): self.wlog = None; return
        t.configure(state='normal'); t.insert('end', line + '\n'); t.see('end'); t.configure(state='disabled')

    # ---- the pages
    def home(self):
        s, b = self.settings, self.build_dir()
        built = [k for k in s.get('built', []) if k in ('windows', 'app', 'ipa', 'xcode', 'signed')]
        if not (built and b and b.is_dir()): return self.p_welcome()
        new = s.get('built_with') and s['built_with'] != tool_version()
        pg = self.page(None, 'Welcome back', f'Your build: {self.made_text(built)}' + (f', from Pokopia {s["game"]}.' if s.get('game') else '.'))
        if new:
            iphone = any(k in built for k in ('ipa', 'signed', 'xcode'))
            self.step_card(pg, 1, f'Update to {tool_version()}', f'Your game was built with {s["built_with"]}. Updating redoes only what changed, so it is much '
                           'quicker than the first build, and your save stays where it is.' +
                           (' iPhone: install the new app over the old one; never delete the old app first, that deletes its save.' if iphone else ''),
                           [('Update my game', lambda: self.p_check(then=self.start_build), 'primary')])
        else:
            n = self.play_cards(pg, built, primary=not s.get('refresh_due'))
            if s.get('refresh_due'):
                self.step_card(pg, n + 1, 'Then refresh the graphics', 'Once you have played, the builder knows which graphics your game made while '
                               'you played. Refreshing prepares them, so nothing is black or missing any more. Usually a few minutes.',
                               [('Refresh now', self.p_refresh, 'primary')])
        more = tk.Frame(pg, bg=C['bg']); more.pack(fill='x', pady=(12, 0))
        Pill(more, 'Make something else', self.p_make, size=10).pack(side='left', padx=(0, 8))
        if not s.get('refresh_due') and any(k in built for k in ('app', 'ipa', 'signed', 'xcode')):
            Pill(more, 'Refresh the graphics', self.p_refresh, size=10).pack(side='left', padx=(0, 8))
        Pill(more, 'Open the build folder', self.open_out, size=10).pack(side='left')

    def play_cards(self, pg, built, primary=True):   # what to do with each thing that was made; returns the last step's number
        n, kind = 0, 'primary' if primary else 'plain'
        if 'windows' in built:
            n += 1; self.step_card(pg, n, 'Bring your save (optional)', 'From Eden, your iPhone or a Mac, so you carry on where you were. Skip it to '
                                   'start a new game. The save the game has now is kept beside it, not deleted.', [('Bring my save', self.wiz_import, 'plain')])
            n += 1; self.step_card(pg, n, 'Play', 'The first minutes can hitch while the game prepares its graphics; it gets smooth after that. '
                                   'F11 or Alt+Enter switches to full screen. "Pokopia Settings" in your build folder changes the window, the controls '
                                   'and Link Play.', [('Play', self.do_play, kind)])
        if 'app' in built:
            n += 1; self.step_card(pg, n, 'Play the Mac app', 'Open it and play into your town. The first time, some graphics can be black or missing: '
                                   'that is expected. The app notes which graphics it makes, and Refresh then prepares them.',
                                   [('Open the Mac app', self.open_mac_app, kind)])
        if any(k in built for k in ('ipa', 'signed', 'xcode')):
            n += 1; self.step_card(pg, n, 'Put the iPhone app on your phone', ('Sign it with your Apple certificate, install it' if 'xcode' not in built else
                                   'Sign it in Xcode, install it') + ', copy the game files over once, then play a few minutes. The guide shows each step.',
                                   ([] if 'xcode' in built else [('Sign the iPhone app', self.wiz_sign, 'plain')]) +
                                   [('Show me how', lambda: webbrowser.open(DOCS + ('WINDOWS.md' if WIN else 'IPHONE.md')), 'plain')])
        return n

    def p_welcome(self):
        pg = self.page(0, 'Welcome to Pokopia Native', f'This turns your own copy of Pokopia into a game that runs on your {PC}. It goes one step at a '
                       'time, and you can stop at any point: the next time you open it, it picks up where you left off.')
        r = self.card(pg)
        label(r.body, 'What you need', 12, True, C['title']).pack(fill='x')
        for t in ('The game files your dumping tool made from your own Switch 2 copy (game version 2.0.1 or 1.0.5)', 'About 15 GB of free space',
                  'Some time: the first build takes 30 to 60 minutes, mostly waiting'):
            label(r.body, '•  ' + t, 11).pack(fill='x', pady=(3, 0))
        self.navb(nxt=self.p_make, text="Let's start")

    def p_make(self):
        pg = self.page(1, 'What do you want to make?', 'Pick one or both. You can add the other later.')
        here = self.windows if WIN else self.mac_app if MAC else None
        def flip(v): v.set(not v.get()); self.save_settings(); self.p_make()
        if here is not None:
            self.choice(pg, f'Play on this {PC}', 'Keyboard and mouse, or an Xbox, PlayStation or Switch Pro controller.' if WIN else
                        'A Mac app. Keyboard and mouse, or a controller.', here.get(), lambda: flip(here), 'recommended')
        self.choice(pg, 'Play on your iPhone', 'An iPhone app you install yourself: ' + ('you sign it here with your Apple certificate.' if WIN else
                    'sign it with your Apple certificate, or with Xcode in Advanced.'), self.ipa.get(), lambda: flip(self.ipa))
        self.navb(self.p_welcome, lambda: self.p_check(then=self.p_files), 'Next', (here is not None and here.get()) or self.ipa.get())

    def p_check(self, then):
        if MAC: self.no_ios.set(not self.ipa.get())
        pg = self.page(2, f'Checking your {PC}', 'Everything is checked for you. If something is missing, it says what to do.')
        self.checks, self.check_then, self.setup_state = [], then, None
        self.check_box = tk.Frame(pg, bg=C['bg']); self.check_box.pack(fill='x')
        self.row(self.check_box, f'Your {PC}', 'wait', 'checking…')
        self.check_next = self.navb(self.p_make, then, 'Next', False)
        if not self.run_wiz(self.do_doctor, line=self.check_line, done=self.check_done): self.check_done(1)

    def check_line(self, line):
        m = re.match(r'^\s+(ok|MISSING|WARNING)\s+(.*)', line)
        if m:
            name, _, hint = m.group(2).partition('   -> ')
            self.checks.append((m.group(1), name.strip(), hint.strip()))

    def check_done(self, rc):
        bad = self.render_checks(rc)
        if bad: return
        nx = pathlib.Path(os.environ.get('NXNATIVE') or HERE / 'nxnative')
        if (nx / 'dm' / 'host' / 'Makefile').is_file() and self.settings.get('setup_with') == self.setup_key(): self.setup_state = ('ok', 'ready')
        else:   # Setup again when this copy, its version or the iPhone choice changed: an update unzipped over an old copy keeps the old host patch
            self.setup_state = ('wait', 'getting them ready, about a minute')
            if not self.run_wiz(self.do_setup, done=self.setup_done): self.setup_state = ('bad', 'could not start', '')
        self.render_checks(rc)

    def setup_key(self): return f'{HERE}|{tool_version()}|{"no-ios" if self.skip_ios() else "ios"}'

    def setup_done(self, rc):
        if rc == 0: self.save_settings(setup_with=self.setup_key())
        self.setup_state = ('ok', 'ready (set up just now)') if rc == 0 else ('bad', 'setup stopped', self.result.get('fail') or f'exit code {rc}')
        if self.check_box.winfo_exists(): self.render_checks(0)

    def render_checks(self, rc):   # returns True when something is missing
        box = self.check_box
        if not box.winfo_exists(): return True
        for c in box.winfo_children(): c.destroy()
        rows, warns = {}, []
        for kind, name, hint in self.checks:
            if kind == 'WARNING': warns.append(name); continue
            nice = next((n for pat, n in CHECK_NAMES if re.search(pat, name)), name)
            if nice is None: continue
            st, hints = rows.get(nice, ('ok', []))
            if kind == 'MISSING': st, hints = 'bad', hints + [hint]
            rows[nice] = (st, hints)
        good = [n for n, (st, _) in rows.items() if st == 'ok'] + (['Builder tools'] if self.setup_state and self.setup_state[0] == 'ok' else [])
        if good:   # what is fine, in one card; what needs you, a card each
            r = self.card(box); grid = tk.Frame(r.body, bg=r.fill); grid.pack(fill='x')
            for i, n in enumerate(good):
                tk.Label(grid, text='✓  ' + n, font=F(11), fg=C['ok'], bg=r.fill, anchor='w').grid(row=i // 2, column=i % 2, sticky='w', padx=(0, 24), pady=2)
            grid.columnconfigure(0, weight=1); grid.columnconfigure(1, weight=1)
        for nice, (st, hints) in rows.items():
            fix, cmd = fix_text(hints)
            if st != 'ok': self.row(box, nice, st, hint=f'To fix: {fix}' if fix else '', copy=cmd or None)
        for w in warns: self.row(box, 'Heads-up', 'warn', 'worth reading', hint=w)
        if not rows: self.row(box, f'Your {PC}', 'bad', 'the check did not run', hint='\n'.join(self.lines[-6:]))
        bad = not rows or any(st == 'bad' for st, _ in rows.values())
        if self.setup_state and self.setup_state[0] != 'ok': self.row(box, 'Builder tools', *self.setup_state)
        ok = not bad and self.setup_state and self.setup_state[0] == 'ok'
        if bad or (self.setup_state and self.setup_state[0] == 'bad'):
            label(box, 'Fix what is marked, then check again. Advanced (top right) has every button if you know your way around.', 10,
                  fg=C['soft']).pack(fill='x', pady=(6, 4))
            Pill(box, 'Check again', lambda: self.p_check(self.check_then), size=10).pack(anchor='w')
        if self.check_next and self.check_next.winfo_exists(): self.check_next.set_enabled(bool(ok))
        return bad

    def p_files(self):
        pg = self.page(3, 'Find your game files', 'Choose the folder your dumping tool made from your own copy of Pokopia. It is only read, never changed.')
        d = self.dump.get().strip()
        r = self.card(pg); row = tk.Frame(r.body, bg=r.fill); row.pack(fill='x')
        btns = tk.Frame(row, bg=r.fill); btns.pack(side='right')
        Pill(btns, 'Choose folder…', lambda: (self.choose_dump(), self.p_files())).pack(side='left')
        Pill(btns, '.nup file…', lambda: (self.choose_nup(), self.p_files()), size=10).pack(side='left', padx=(6, 0))
        label(row, d or 'No folder chosen yet', 11, fg=C['text'] if d else C['soft']).pack(side='left', fill='x', expand=True)
        self.dump_row = tk.Frame(pg, bg=C['bg']); self.dump_row.pack(fill='x')
        b = self.build_dir()
        r2 = self.card(pg, pady=(10, 4)); top = tk.Frame(r2.body, bg=r2.fill); top.pack(fill='x')
        Pill(top, 'Change…', lambda: (self.choose_out(), self.p_files()), size=10).pack(side='right')
        label(top, 'Where your game gets built', 12, True, C['title'], wrap=False).pack(side='left')
        label(r2.body, (f'{b}  (about 12 GB)' if b else 'Next to your game files, once you choose them.'), 10, fg=C['soft']).pack(fill='x', pady=(4, 0))
        need = 1 if b and (b / 'extract' / 'RomFS.img').is_file() else 15
        free = self.free_gb(b)
        self.space_ok = free is None or free >= need
        if free is not None:
            self.row(pg, 'Free space there', 'ok' if self.space_ok else 'bad', f'{free:.0f} GB free',
                     hint='' if self.space_ok else f'It needs about {need} GB. Free up some space, or press Change… to build on another drive.')
        if WIN and b and re.search(r'\\(Documents|Desktop|Downloads|OneDrive)(\\|$)', str(b), re.I):
            self.row(pg, 'A tip', 'warn', 'Windows Security', hint='Windows Security can block writing in Documents, Desktop and Downloads. If the build '
                     'stops with "access denied", press Change… and choose a folder like C:\\Games.')
        self.files_next = self.navb(self.p_make, self.p_build, 'Next', False)
        self.check_files()

    @staticmethod
    def free_gb(b):
        if not b: return None
        p = pathlib.Path(b)
        while not p.exists() and p.parent != p: p = p.parent
        try: return shutil.disk_usage(p).free / (1 << 30)
        except OSError: return None

    def check_files(self):
        d = self.dump.get().strip()
        if not d: return
        if self.dump_info and self.dump_info[0] == d: return self.show_files(self.dump_info)
        self.row(self.dump_row, 'Your game files', 'wait', 'looking…')
        def work():
            info = self.detect(d)
            self.after(0, lambda: self.show_files(info))
        threading.Thread(target=work, daemon=True).start()

    def detect(self, d):
        """(folder, state, text): which game version a dump holds, by the builder's own check (nxs2.find_dump)"""
        p = pathlib.Path(d).expanduser()
        if p.is_file() and p.suffix.lower() == '.nup': return (d, 'ok', 'a .nup file (unpacked when the build starts)')
        if not p.is_dir(): return (d, 'bad', 'This folder is not there any more. Choose it again.')
        try:
            if str(HERE) not in sys.path: sys.path.insert(0, str(HERE))
            import nxs2
            _, _, ver, disp = nxs2.find_dump(argparse.Namespace(dump=str(p), exefs=None, romfs=None))
            return (d, 'ok', f'Pokopia {disp if disp and disp != "?" else ver}')
        except Exception as e:
            return (d, 'bad', f'These are not game files the builder knows. {e}')

    def show_files(self, info):
        self.dump_info = info
        if not self.dump_row.winfo_exists() or info[0] != self.dump.get().strip(): return
        for c in self.dump_row.winfo_children(): c.destroy()
        if info[1] == 'ok': self.row(self.dump_row, 'Found', 'ok', info[2])
        else: self.row(self.dump_row, 'Your game files', 'bad', 'not recognised', hint=info[2])
        self.files_next.set_enabled(info[1] == 'ok' and self.space_ok)

    def p_build(self):
        pg = self.page(4, 'Ready to build')
        made = [k for k, v in (('windows', WIN and self.windows.get()), ('app', MAC and self.mac_app.get()), ('ipa', self.ipa.get())) if v]
        self.row(pg, 'Making', 'info', self.made_text(made))
        if self.dump_info and self.dump_info[1] == 'ok': self.row(pg, 'From', 'info', self.dump_info[2])
        label(pg, 'How fast should it build?', 12, True, C['title']).pack(fill='x', pady=(12, 2))
        def speed(v): self.full_speed.set(v); self.p_build()
        micro = any(k == 'WARNING' and 'microcode' in n for k, n, _ in self.checks)
        self.choice(pg, 'Gentle', f'Your {PC} stays cool and usable while it builds. The first build often takes 30 to 60 minutes.',
                    not self.full_speed.get(), lambda: speed(False), 'recommended')
        self.choice(pg, 'Full speed', 'Uses every core: often 20 to 40 minutes, and the fans get loud.' +
                    (' Your processor needs a BIOS update first (the heads-up in the checks), so stay on Gentle.' if micro else ''),
                    self.full_speed.get(), lambda: speed(True))
        label(pg, 'Building again later, after an update, only redoes what changed, so it is much quicker.', 10, fg=C['soft']).pack(fill='x', pady=(8, 0))
        self.navb(self.p_files, self.start_build, 'Start building')

    def start_build(self):
        if MAC: self.no_ios.set(not self.ipa.get())
        i = self.dump_info
        if i and i[1] == 'ok' and i[0] == self.dump.get().strip() and i[2].startswith('Pokopia '): self.save_settings(game=i[2][8:])
        self.prog = {'kind': 'Build', 'stages': [s for s in BUILD_STAGES if WIN or not s[0].startswith('Recompiling')], 'cur': None, 'pct': None,
                     'shaders': None, 'dirty': True, 'first': not self.settings.get('built_with')}
        self.prog['state'] = ['wait'] * len(self.prog['stages'])
        if self.run_wiz(self.do_build, line=self.prog_line, done=self.prog_done, tick=self.prog_tick): self.p_progress()

    def p_refresh(self):
        built = self.settings.get('built', [])
        pg = self.page(None, 'Refresh the graphics', 'The game notes each graphic it makes while you play. Refreshing prepares them all, so nothing is '
                       'black or missing any more. Usually a few minutes.')
        n = 0
        if 'app' in built:
            n += 1; self.step_card(pg, n, 'The Mac app', 'Nothing to do here: its notes are found by themselves. Play into your town first if you have not yet.')
        if any(k in built for k in ('ipa', 'signed', 'xcode')):
            n += 1; self.step_card(pg, n, 'The iPhone app', 'First copy the notes over from your iPhone. The guide shows how.',
                                   [('Show me how', lambda: webbrowser.open(DOCS + ('WINDOWS.md' if WIN else 'IPHONE.md')), 'plain')])
        self.navb(self.home, self.start_refresh, 'Refresh now')

    def start_refresh(self):
        self.prog = {'kind': 'Refresh', 'stages': list(REFRESH_STAGES), 'cur': None, 'pct': None, 'shaders': None, 'dirty': True}
        self.prog['state'] = ['wait'] * len(self.prog['stages'])
        if self.run_wiz(self.do_refresh, line=self.prog_line, done=self.prog_done, tick=self.prog_tick): self.p_progress()

    def p_progress(self):
        refresh = self.prog['kind'] == 'Refresh'
        pg = self.page(None if refresh else 4, 'Refreshing the graphics' if refresh else 'Building your game',
                       f'You can keep using your {PC}. Closing this window is safe: start again and it carries on where it stopped.')
        self.stage_box = tk.Frame(pg, bg=C['bg']); self.stage_box.pack(fill='x')
        self.elapsed = label(pg, '', 10, fg=C['soft']); self.elapsed.pack(fill='x', pady=(6, 0))
        self.details_area(pg)
        Pill(self.nav, 'Stop', self.do_stop).pack(side='left')
        self.prog['dirty'] = True; self.prog_tick()

    def prog_line(self, line):
        p = self.prog
        if self.result.get('fail') and line.strip(): p.setdefault('after', []).append(line.strip())   # the failing tool's last lines
        m = re.match(r'^\[\d+:\d\d(?::\d\d)?\] (.*)', line)
        if m:
            msg = m.group(1)
            for i, (_, pat, _) in enumerate(p['stages']):
                if re.search(pat, msg) and p['state'][i] != 'done':
                    if p['cur'] is not None and p['cur'] != i: p['state'][p['cur']] = 'done'
                    p['cur'], p['state'][i], p['pct'] = i, 'run', None
                    break
            sh = re.search(r'(\d+) of the (\d+) known run-time shaders', msg)
            if sh: p['shaders'] = (int(sh.group(1)), int(sh.group(2)))
            pc = re.fullmatch(r'\s*(\d+)%', msg)   # a .nup being unpacked
            if pc: p['pct'] = int(pc.group(1)) / 100
        elif line.startswith('    '):   # a tool's own progress, "n/total" (the recompile's parts, the shader chain)
            nt = re.search(r'\b(\d+)\s*/\s*(\d+)\b', line)
            if nt and int(nt.group(2)) >= 20 and int(nt.group(1)) <= int(nt.group(2)): p['pct'] = int(nt.group(1)) / int(nt.group(2))
        p['dirty'] = True

    def prog_tick(self):
        p = self.prog
        if not p or not getattr(self, 'stage_box', None) or not self.stage_box.winfo_exists(): return
        if self.t0: el = int(time.time() - self.t0); self.elapsed.configure(text=f'Running for {el // 60}:{el % 60:02d}.')
        if p.get('dirty'): p['dirty'] = False; self.render_stages()
        self.draw_bar()

    def render_stages(self):
        for c in self.stage_box.winfo_children(): c.destroy()
        p, self.bar = self.prog, None
        for i, (name, _, note) in enumerate(p['stages']):
            st = p['state'][i]
            r = Round(self.stage_box, fill=C['light'] if st == 'run' else C['card'], edge=C['sel'] if st == 'run' else None, pad=10, radius=12)
            r.pack(fill='x', pady=3)
            top = tk.Frame(r.body, bg=r.fill); top.pack(fill='x')
            label(top, name, 11, st == 'run', C['title'] if st == 'run' else C['text'], wrap=False).pack(side='left')
            text, fg = {'done': ('✓ done', C['ok']), 'run': (f'{round(p["pct"] * 100)}%' if p['pct'] is not None else 'working…', C['seltext']),
                        'wait': ('waiting', C['soft'])}[st]
            tk.Label(top, text=text, font=F(11, st == 'done'), fg=fg, bg=r.fill).pack(side='right')
            if st == 'run':
                self.bar = tk.Canvas(r.body, height=8, bg=r.fill, highlightthickness=0); self.bar.pack(fill='x', pady=(6, 0))
                if note: label(r.body, note, 10, fg=C['soft']).pack(fill='x', pady=(4, 0))

    def draw_bar(self):   # the running stage's bar: its share done when the tool says, else a band sliding along
        b = self.bar
        if not b or not b.winfo_exists(): return
        w = b.winfo_width()
        if w < 20: return
        b.delete('all'); rrect(b, 0, 0, w - 1, 7, 4, fill=C['pill'], outline=C['pill'])
        pct = self.prog['pct']
        if pct is not None: x0, x1 = 0, max(8, (w - 1) * pct)
        else: x = (time.time() * 140) % (w + 160) - 160; x0, x1 = max(0, x), min(w - 1, x + 160)
        if x1 - x0 >= 8: rrect(b, x0, 0, x1, 7, 4, fill=C['sel'], outline=C['sel'])

    def prog_done(self, rc):
        p = self.prog
        if rc == 0:
            p['state'] = ['done'] * len(p['stages'])
            if p['kind'] == 'Build':
                made = [k for k in ('windows', 'app', 'ipa', 'xcode') if k in self.result]
                apple = any(k in made for k in ('app', 'ipa', 'xcode'))
                self.save_settings(built=sorted(set(self.settings.get('built', [])) | set(made)), built_with=tool_version(),
                                   refresh_due=bool(self.settings.get('refresh_due')) or (apple and p['first']))
                return self.p_play(made)
            self.save_settings(refresh_due=False)
            return self.p_refreshed()
        retry = self.start_build if p['kind'] == 'Build' else self.start_refresh
        if self.result.get('stopped'):
            self.page(None if p['kind'] == 'Refresh' else 4, f'{p["kind"]} paused', 'Everything done so far is kept. Carry on now or later: it starts '
                      'where it stopped.')
            return self.navb(self.home, retry, 'Carry on')
        pg = self.page(None if p['kind'] == 'Refresh' else 4, f'The {p["kind"].lower()} stopped with an error',
                       'Nothing is lost: everything done so far is kept, and trying again carries on from there.')
        msg = '\n'.join([self.result.get('fail') or f'It ended with exit code {rc}.'] + p.get('after', [])[-6:])
        r = self.card(pg, C['badbg'], C['bad'])
        label(r.body, 'What went wrong', 12, True, C['bad']).pack(fill='x')
        label(r.body, '\n'.join(msg.splitlines()[:8]), 10).pack(fill='x', pady=(4, 0))
        low = tk.Frame(pg, bg=C['bg']); low.pack(fill='x', pady=(6, 0))
        report = (f'Pokopia Native {tool_version()} ({sys.platform}), {p["kind"].lower()} failed:\n{msg}\n\nThe last lines:\n' + '\n'.join(self.lines[-30:]))
        Pill(low, 'Copy the error', lambda: self.copy(report), size=10).pack(side='left', padx=(0, 8))
        Pill(low, 'Ask in the Discord', lambda: webbrowser.open(DISCORD), size=10).pack(side='left')
        label(pg, 'When you ask for help, paste the error and say which computer you have. The full output of each stage is in the logs folder inside '
              'your build folder.', 10, fg=C['soft']).pack(fill='x', pady=(8, 0))
        self.details_area(pg)
        self.navb(self.p_build if p['kind'] == 'Build' else self.home, retry, 'Try again')

    def p_play(self, made):
        apple = any(k in made for k in ('app', 'ipa', 'xcode'))
        pg = self.page(5, 'Your game is ready!', 'One more step after your first play, and the builder reminds you.' if apple and self.settings.get('refresh_due')
                       else 'A couple of optional things first, then play.' if 'windows' in made else 'Here is what to do next.')
        n = self.play_cards(pg, made)
        if apple and self.settings.get('refresh_due'):
            self.step_card(pg, n + 1, 'Then come back here and press Refresh', 'It prepares the graphics your game noted while you played, usually in a '
                           'few minutes. When you open the builder again, this step is waiting for you.')
        self.navb(nxt=self.home, text='Done')

    def p_refreshed(self):
        built = self.settings.get('built', [])
        pg = self.page(None, 'All set', 'The graphics are ready.' + (' Open the Mac app again to play.' if 'app' in built else '') +
                       (' iPhone: install the new app over the old one; never delete the old app first, that deletes its save.'
                        if any(k in built for k in ('ipa', 'signed', 'xcode')) else ''))
        sh = self.prog.get('shaders')
        if sh:
            full = sh[0] >= sh[1]
            self.row(pg, 'Graphics recorded', 'ok' if full else 'info', f'{sh[0]} of {sh[1]}', hint='' if full else
                     'Places you have not been to yet still need recording. After playing further, refresh again.')
        self.navb(nxt=self.home, text='Done')

    def wiz_import(self):
        self.run_wiz(self.do_import, done=lambda rc: messagebox.showinfo('Your save', 'Your save is in place. Press Play to carry on where you were.')
                     if rc == 0 else messagebox.showerror('Your save', self.result.get('fail') or f'exit code {rc}'))

    def wiz_sign(self):
        def done(rc):
            if rc == 0:
                self.save_settings(built=sorted(set(self.settings.get('built', [])) | {'signed'}))
                messagebox.showinfo('Signed', f'The signed iPhone app is in the build folder:\n{self.result.get("signed", "")}\n\nThe guide shows how to install it.')
            else: messagebox.showerror('Signing stopped', self.result.get('fail') or f'exit code {rc}')
        self.run_wiz(self.do_sign, done=done)

    def open_mac_app(self):
        b = self.build_dir(); app = b / 'out' / f'{NAME}.app' if b else None
        if not app or not app.is_dir(): return messagebox.showwarning('Mac app', 'There is no Mac app in the build folder yet.')
        subprocess.Popen(['open', str(app)])


def _self_shot(app, path):   # NXS2_GUI_SHOT=<png>: save a picture of the window and quit (the project's own layout check; needs Pillow)
    from PIL import ImageGrab
    app.update_idletasks(); app.update()
    x, y = app.winfo_rootx(), app.winfo_rooty(); w, h = app.winfo_width(), app.winfo_height()
    k = ImageGrab.grab().size[0] / app.winfo_screenwidth()   # a scaled display: the grab counts real pixels
    ImageGrab.grab(bbox=tuple(round(v * k) for v in (x, y, x + w, y + h))).save(path); app.destroy()


if __name__ == '__main__':
    try:
        log_line(f'window: Python {sys.version.split()[0]}, Tk {tk.TkVersion}, pokopia-native {tool_version()}')
        if not NXS2.is_file(): raise FileNotFoundError(f'nxs2.py not found beside {__file__}: unzip the whole download and start the builder from that folder')
        app = App()
        app.report_callback_exception = lambda *e: report_crash('The window hit an error')
        if os.environ.get('NXS2_GUI_SHOT'):
            app.attributes('-topmost', True); app.lift(); app.focus_force()
            app.after(2500, lambda: _self_shot(app, os.environ['NXS2_GUI_SHOT']))
        log_line('window: open')
        app.mainloop()
    except Exception:
        report_crash('The window could not start'); sys.exit(1)
