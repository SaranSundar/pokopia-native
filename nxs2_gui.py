#!/usr/bin/env python3
"""nxs2_gui.py: the window around nxs2.py. Pick your dump folder, tick what you want, press Build; the stages scroll by
in the window. Everything is done by nxs2.py underneath (the same commands the README lists), so a build started here
can be continued from Terminal and the other way round.

Mac: double-click "Pokopia Native Builder.app" (or: python3.12 nxs2_gui.py). Windows: "Pokopia Native Builder.cmd" (the Windows game and the iPhone app).
Linux: the Pokopia Native Builder AppImage (the Linux game; docs/LINUX.md), or python3 nxs2_gui.py from the "for Linux" download.
Needs Python 3.12+ with Tk (macOS Homebrew: brew install python-tk@3.12; python.org installers have it).
"""
import json, os, pathlib, queue, re, subprocess, sys, threading, time, webbrowser
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

HERE = pathlib.Path(__file__).resolve().parent
NXS2 = HERE / 'nxs2.py'
TITLE = '0400A3A01EE4C000'
NAME = 'Pokemon Pokopia'
MAC = sys.platform == 'darwin'
WIN = sys.platform == 'win32'
LINUX = sys.platform.startswith('linux')
SETTINGS = pathlib.Path.home() / ('.nxs2_gui.json')
LOG = HERE / 'builder-log.txt'   # Windows: the launcher's start-up checks and the window's own errors, for a report
DISCORD = 'https://discord.gg/Aj9GqxZj8V'   # the community server: install help, bug reports, co-op partners


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


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f'Pokopia Native Builder {tool_version()}')
        self.minsize(760, 560)
        self.proc = None; self.q = queue.Queue(); self.t0 = None; self.running = None
        self.settings = self.load_settings()
        self.build_ui()
        self.after(100, self.poll)
        self.check_setup()

    # ---------------------------------------------------------------- settings
    def load_settings(self):
        try: return json.loads(SETTINGS.read_text())
        except Exception: return {}

    def save_settings(self):
        s = {'dump': self.dump.get(), 'out': self.out.get(), 'mac_app': self.mac_app.get(), 'ipa': self.ipa.get(), 'xcode': self.xcode.get(),
             'team': self.team.get(), 'no_ios': self.no_ios.get(), 'windows': self.windows.get(), 'linux': self.linux.get()}
        try: SETTINGS.write_text(json.dumps(s))
        except Exception: pass

    # ---------------------------------------------------------------- the window
    def build_ui(self):
        s = self.settings
        pad = {'padx': 8, 'pady': 4}
        f = ttk.Frame(self); f.pack(fill='both', expand=True, padx=10, pady=10)

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
        self.mac_app = tk.BooleanVar(value=s.get('mac_app', MAC)); self.ipa = tk.BooleanVar(value=s.get('ipa', MAC) if not LINUX else False)
        self.windows = tk.BooleanVar(value=s.get('windows', WIN)); self.linux = tk.BooleanVar(value=LINUX)
        self.xcode = tk.BooleanVar(value=s.get('xcode', False)); self.team = tk.StringVar(value=s.get('team', ''))
        self.no_ios = tk.BooleanVar(value=s.get('no_ios', False))
        self.full_speed = tk.BooleanVar(value=False)   # never remembered: every session starts gentle
        if MAC: ttk.Checkbutton(o, text='Mac app', variable=self.mac_app).pack(side='left', padx=(0, 14))
        if WIN: ttk.Checkbutton(o, text='Windows: play on this PC', variable=self.windows).pack(side='left', padx=(0, 14))
        if LINUX: ttk.Checkbutton(o, text='Linux: play on this computer (the Steam Deck too)', variable=self.linux).pack(side='left', padx=(0, 14))
        else: ttk.Checkbutton(o, text='iPhone IPA (the iPhone app, unsigned)', variable=self.ipa).pack(side='left', padx=(0, 14))
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
        for key, text, cmd in (('doctor', 'Check tools', self.do_doctor), ('setup', 'Setup', self.do_setup), ('build', 'Build', self.do_build)) + \
                              ((('refresh', 'Refresh shaders', self.do_refresh), ('sign', 'Sign IPA', self.do_sign)) if not LINUX else ()) + \
                              ((('play', 'Play', self.do_play), ('import', 'Import save…', self.do_import)) if WIN or LINUX else ()):
            self.buttons[key] = ttk.Button(b, text=text, command=cmd); self.buttons[key].pack(side='left', padx=(0, 6))
        self.stop_btn = ttk.Button(b, text='Stop', command=self.do_stop, state='disabled'); self.stop_btn.pack(side='left', padx=(0, 6))
        ttk.Button(b, text='Open build folder', command=self.open_out).pack(side='right')
        steps = ('Setup once for each new download, then Build (the first build takes a while; stopping is safe, Build again carries on). '
                 'Then Play' + (', or Sign IPA' if WIN else '') + ' for the iPhone app. Import save brings your save from Eden, the iPhone or the Mac. '
                 'Refresh shaders finishes the iPhone app after a first play; the README explains.' if WIN else
                 'Build (the first build takes a while; stopping is safe, Build again carries on), then Play. Import save brings your save from Eden, '
                 'the iPhone or a Mac. docs/LINUX.md explains each step, and adding the game to Steam.' if LINUX else
                 'Setup once for each new download, then Build (the first build takes a while; stopping is safe, Build again carries on). '
                 'Sign IPA makes the iPhone app installable; Refresh shaders finishes it after a first play. The README explains each step.')
        ttk.Label(f, text=steps, foreground='#666', wraplength=720, justify='left').grid(row=10, column=0, columnspan=3, sticky='w', padx=8)

        self.status = tk.StringVar(value='')
        ttk.Label(f, textvariable=self.status, font=('', 11)).grid(row=11, column=0, columnspan=3, sticky='w', padx=8, pady=(6, 2))
        self.log = tk.Text(f, height=18, wrap='none', font=('Menlo' if MAC else 'Consolas' if WIN else 'DejaVu Sans Mono', 10), state='disabled')
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
        self.after(150, self.poll)

    def status_text(self):
        el = int(time.time() - self.t0); el = f'{el // 60}:{el % 60:02d}'
        return f'{self.running}… {el}   {self.last_stage}'

    def on_line(self, line):
        self.write(line)
        m = re.match(r'^\[\d+:\d\d(?::\d\d)?\] (.*)', line)
        if m: self.last_stage = m.group(1)[:90]
        for key, pat in (('app', r'Mac app: (.*)'), ('ipa', r'IPA: (.*?) \('), ('xcode', r'Xcode project: (.*)'), ('signed', r'signed IPA: (.*?)  '),
                         ('windows', r'Windows game: (.*?)  \(save'), ('linux', r'Linux game: (.*?)  \(save')):
            m = re.search(pat, line)
            if m: self.result[key] = m.group(1)
        if line.startswith('[FAIL]'): self.result['fail'] = line[7:]

    def finished(self, rc):
        self.proc = None
        for k in self.buttons.values(): k['state'] = 'normal'
        self.stop_btn['state'] = 'disabled'
        el = int(time.time() - self.t0); el = f'{el // 60}:{el % 60:02d}'
        if rc == 0:
            made = ', '.join(v for k, v in self.result.items() if k != 'fail')
            self.status.set(f'{self.running} done in {el}.' + (f'  {made}' if made else ''))
            if self.running == 'Build' and made:
                nxt = ' '.join(t for k, t in (('windows', 'Press Play to start the game.'), ('linux', 'Press Play to start the game.'), ('app', 'The Mac app is in the build folder.'),
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

    def skip_ios(self):   # Windows: no iPhone toolchain unless the IPA is ticked; Mac: the "Mac only" box; Linux makes no iPhone app
        return LINUX or (WIN and not self.ipa.get()) or (MAC and self.no_ios.get())

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
        if LINUX and self.linux.get(): args.append('--linux')
        if self.ipa.get() and not LINUX: args.append('--ipa')
        if MAC and self.xcode.get():
            if not re.fullmatch(r'[A-Z0-9]{10}', self.team.get().strip()): return messagebox.showwarning('Team ID', 'The Xcode project needs your Apple developer team ID (10 characters).')
            args += ['--xcode', '--team-id', self.team.get().strip()]
        if MAC and self.no_ios.get(): args.append('--no-ios')
        if self.full_speed.get(): args.append('--full-speed')
        if not any(a in args for a in ('--mac-app', '--ipa', '--xcode', '--windows', '--linux')): return messagebox.showwarning('Outputs', 'Tick at least one output.')
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
        if LINUX: return self.play_linux()
        cmd = self.win_launcher()
        if not cmd: return messagebox.showwarning('Play', 'No Windows game yet: tick "Windows: play on this PC" and press Build first.')
        subprocess.Popen(['cmd', '/c', str(cmd)], creationflags=subprocess.CREATE_NEW_CONSOLE, cwd=str(cmd.parent))
        self.status.set(f'Started {cmd.name}. Its console window shows the controls and where the log is.')

    def play_linux(self):   # the build's launcher on its own (its own process group): this window stays free; the Link Play choice is its dialog
        b = self.build_dir(); out = b / 'out' if b else None
        hits = sorted(out.glob('play-pokopia-*.sh')) if out and out.is_dir() else []
        if not hits: return messagebox.showwarning('Play', 'No Linux game yet: tick "Linux: play on this computer" and press Build first.')
        log = b / 'logs' / 'play-launcher.txt'
        try:
            with open(log, 'w', encoding='utf-8') as f:
                subprocess.Popen([str(hits[-1])], stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT, cwd=str(hits[-1].parent), start_new_session=True)
        except OSError as e: return messagebox.showerror('Play', f'{e}')
        self.status.set(f'Started {hits[-1].name}. The game\'s own log is in ~/.cache/pokopia-native; the launcher\'s in {log}.')

    def do_import(self):
        b = self.build_dir()
        game = 'linux' if LINUX else 'windows'
        if not b or not (b / game / 'play.json').is_file():
            return messagebox.showwarning('Import save', f'Build the {"Linux" if LINUX else "Windows"} game first: the save goes beside it.')
        eden = messagebox.askyesnocancel('Import save', "Take the save from Eden on this PC?\n\nYes: Eden's own save of Pokopia (read, never changed).\n"
                                         'No: choose a folder (one holding B00, G00 and S00: a phone or Mac save, or a copy).\n\n'
                                         f'The save the {"Linux" if LINUX else "Windows"} game has now is kept beside it, not deleted.')
        if eden is None: return
        if eden: args = ['import-save', str(b), '--eden']
        else:
            d = filedialog.askdirectory(title='The save folder (B00, G00, S00 inside)')
            if not d: return
            args = ['import-save', str(b), d]
        self.last_stage = 'starting'; self.start('Import save', args)


def _self_shot(app, path):   # NXS2_GUI_SHOT=<png>: save a picture of the window and quit (the project's own layout check; needs Pillow)
    from PIL import ImageGrab
    app.update_idletasks(); app.update()
    x, y = app.winfo_rootx(), app.winfo_rooty(); w, h = app.winfo_width(), app.winfo_height()
    try: ImageGrab.grab(bbox=(x, y, x + w, y + h), **({'xdisplay': os.environ.get('DISPLAY')} if LINUX else {})).save(path)   # Linux: from the X server
    except Exception as e: print(f'NXS2_GUI_SHOT: {e}', file=sys.stderr)
    app.destroy()


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
