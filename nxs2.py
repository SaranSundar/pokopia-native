#!/usr/bin/env python3
"""nxs2.py: build Pokemon Pokopia (Nintendo Switch 2) for your own Mac, iPhone and Windows PC, from your own dump.

Everything derived from the game is made here, on your machine, from the dump you give; nothing of the
game is in this repository (see LEGAL.md). The build goes through nxnative (the native host for Switch
games on Apple systems, with this project's Switch 2 changes), the game's shaders through ampere-lower
(Eden-NS2's Switch 2 shader lowering) and nxnative's Metal shader chain.

  nxs2.py setup [--no-ios]                  fetch nxnative at the pinned commit, apply the Switch 2 patch, build ampere-lower
  nxs2.py doctor                            check the tools this computer has (what setup and build need)
  nxs2.py build <dump> [options]            the whole build into <dump folder>/<title>_native (or --out)
       --mac-app         a double-clickable Mac app (the game files cloned inside it)
       --ipa             an unsigned IPA with the entitlements the game needs (sign it with your own team)
       --windows         Windows: the game recompiled for this PC, with a launcher (out/Play Pokopia <version>.cmd)
       --xcode --team-id ID   an Xcode project signed with your Apple developer team (then: nxs2.py sign)
       --dumps DIR...    shader dumps from an earlier run (DM_NVN_SHDUMP folders) to include from the start
       --no-ios          Mac libraries only (faster; --ipa / --xcode need the iOS ones)
  nxs2.py run <build dir>                   play in a window on this Mac; new shaders are recorded for refresh
  nxs2.py refresh <build dir> [DIR...]      add the shaders recorded while playing (Mac run, Mac app, iPhone) and rebuild the outputs
  nxs2.py sign <build dir> --team-id ID     archive and export the development IPA from the prepared Xcode project
  nxs2.py status <build dir>                what the build folder holds and what it still lacks
  nxs2.py import-save <build dir> <folder>  Windows: a save from Eden, the phone or a Mac (a folder with B00, G00, S00) becomes the one the game plays

Needs: a Mac with Apple silicon, Xcode (27.x) with its command line tools (an iPhone build also needs the iOS
platform), Python 3.12 or newer with capstone (5.x), lz4 and fontTools, the .NET 10 runtime (the shader
translator), xcodegen for the iPhone build. On Windows the iPhone build needs the toolchain in docs/WINDOWS.md and the
Windows game the tools of the "for Windows PCs" download (windows/bin). `nxs2.py doctor` lists what is missing.
"""
import argparse, concurrent.futures as cf, hashlib, json, os, pathlib, platform, re, shlex, shutil, struct, subprocess, sys, time

TOOL_VERSION = '1.3.5'   # this tool's release (co-op compares the game build and mods, not this); the game's own version is separate
TITLE = '0400A3A01EE4C000'
NAME = 'Pokemon Pokopia'
UPSTREAM = 'https://github.com/seedds/nxnative'
UPSTREAM_COMMIT = 'c1df4dd'   # the nxnative commit host/nxnative-switch2.patch is made against
FORK = 'https://github.com/zenvyzenshiro/NXNativeWindows'   # nxnative's Windows port: its win/ folder builds the iPhone app with LLVM, Metal Developer Tools for Windows and .NET
FORK_COMMIT = '269d61ff974b2e8e073d7357466ae130c3f6812f'   # fetched by its full id (a sparse checkout of its win/ folder only)
WIN = sys.platform == 'win32'
HERE = pathlib.Path(__file__).resolve().parent
PROFILE_SRC = HERE / 'profile' / TITLE
TOOLS = HERE / 'tools'
AMPERE = HERE / 'ampere-lower' / ('windows/ampere-lower.exe' if WIN else 'ampere-lower')
NX = pathlib.Path(os.environ.get('NXNATIVE') or HERE / 'nxnative')
PY = sys.executable
if WIN and pathlib.Path(PY).name.lower() == 'pythonw.exe' and pathlib.Path(PY).with_name('python.exe').is_file():
    PY = str(pathlib.Path(PY).with_name('python.exe'))   # the tools' Python has a console: started without one, every console tool it runs opens a window
NO_WINDOW = subprocess.CREATE_NO_WINDOW if WIN else 0   # the tools' consoles stay hidden: their output is captured anyway
WIN_KIT = HERE / 'windows' / 'bin'   # the Windows game's tools as the "for Windows PCs" download ships them (tools/package_release.py makes it)
T0 = time.time()

# the shader tables a profile has before the package is made (as nxnative's NSP script writes them for a provisional profile)
EMPTY_TABLES = {
    'shader_map.tsv': '# kind: evidence  filled by the shader package (nxs2.py package)\npackage\tindex\tstage\tdm_ctl_sha16\tsource\n',
    'shader_facts.tsv': '# Answer table for translation queries: key <TAB> fact JSON. Filled by the shader package (nxs2.py package)\n',
    'shader_pairs.tsv': '# kind: evidence  vertex / pixel shader pairs within BNSH variations: vertex key <TAB> pixel key\n',
    'shader_map.tsv.conflicts.tsv': '# kind: evidence  control-section key conflict rulings: empty\ndm_ctl_sha16\tchosen_pc\tclass\tcandidates\trecords\n',
    'bf_want.tsv': '# generated\n',
    'pso_manifest.tsv': '# Prewarm seed list (shipped in the app, next to shader_map.tsv). Rows: S<TAB>shader key<TAB>stage / R<TAB>render pipeline key (pso_skey hex) / C<TAB>compute shader key.\n'
                        '# Empty until recorded on the Mac (DM_PSO_MANIFEST=off DM_PSO_RECORD).\n'}
# switches baked into the apps (the player's env.txt overrides them): the shaders the game makes while it runs are recorded for `nxs2.py refresh`
APP_ENV = '# nxs2.py: switches the build bakes in; Documents/env.txt (iPhone) or Application Support/nxnative/<title>/env.txt (Mac) override them\nDM_NVN_SHDUMP=shdump\n'
PHONE_ENV = APP_ENV + 'DM_OPMODE=0\nDM_FPS=30\n'   # handheld mode and 30 fps on the phone: what the Switch 2 does in handheld, and what keeps the phone cool


class Fail(Exception): pass


def hms(s):
    s = int(s); return f'{s // 60}:{s % 60:02d}' if s < 3600 else f'{s // 3600}:{s // 60 % 60:02d}:{s % 60:02d}'


def say(msg): print(f'[{hms(time.time() - T0)}] {msg}', flush=True)


def run(cmd, log, env=None, cwd=None, ok=(0,), heartbeat=30, show=None):
    """Run cmd with its output in log (a file), a heartbeat line on the console while it runs (and the lines show matches, or all of them with
    -v). A non-zero exit not in ok stops the build with the log's last lines."""
    cmd = [str(c) for c in cmd]
    log = pathlib.Path(log); log.parent.mkdir(parents=True, exist_ok=True)
    e = dict(os.environ); e.update(env or {})
    last, t_beat, t_start = '', time.time(), time.time()
    with open(log, 'w', encoding='utf-8', errors='replace') as f:
        f.write('$ ' + ' '.join(cmd) + '\n'); f.flush()
        p = subprocess.Popen(cmd, env=e, cwd=str(cwd) if cwd else None, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace',
                             creationflags=NO_WINDOW)
        for line in p.stdout:
            f.write(line); last = line.rstrip()
            if VERBOSE or (show and re.search(show, line)): print('    ' + last[:160], flush=True)
            if time.time() - t_beat > heartbeat:
                t_beat = time.time(); print(f'    ... {hms(time.time() - t_start)}  {last[:120]}', flush=True)
        rc = p.wait()
    if rc not in ok:
        tail = ''.join(log.read_text(encoding='utf-8', errors='replace').splitlines(keepends=True)[-25:])
        raise Fail(f'{pathlib.Path(cmd[0]).name} failed ({rc}); log: {log}\n{tail}')
    return rc


def which(name, *extra):
    p = shutil.which(name)
    if p: return p
    for x in extra:
        x = pathlib.Path(os.path.expanduser(x))
        if x.is_file() and os.access(x, os.X_OK): return str(x)
    return None


def dotnet_on_path():
    """Windows: .NET installed while the builder window was open isn't on that window's PATH (Windows gives the new PATH only to programs
    started afterwards), so "Check again" never found it. Its installer's folder goes on PATH for this run and everything it starts"""
    if not WIN or shutil.which('dotnet'): return
    d = pathlib.Path(os.environ.get('ProgramFiles', r'C:\Program Files')) / 'dotnet'
    if (d / 'dotnet.exe').is_file(): os.environ['PATH'] = f'{d}{os.pathsep}{os.environ.get("PATH", "")}'


def dotnet_root():
    """The .NET install that holds the runtime (shared/Microsoft.NETCore.App): the one on PATH, ~/.dotnet, the installer's, or Homebrew's. Homebrew's
    formula puts a wrapper script on PATH (its bin/dotnet sets DOTNET_ROOT to ../libexec), so a folder counts only when the runtime is in it (#26)"""
    p = shutil.which('dotnet')
    near = [pathlib.Path(p).resolve().parent] if p else []
    near += [d.parent / 'libexec' for d in near]
    for d in near + [pathlib.Path(os.path.expanduser(x)) for x in ('~/.dotnet', '/usr/local/share/dotnet', '/opt/homebrew/opt/dotnet/libexec', '/usr/local/opt/dotnet/libexec')]:
        if any((d / n).is_file() for n in ('dotnet', 'dotnet.exe')) and (d / 'shared' / 'Microsoft.NETCore.App').is_dir(): return d
    return None


# ---- Windows: where the fork's toolchain lives (win/toolchain.py reads the same names: NXW_IOS_SDK, SPIRV_CROSS, win/toolchain.json)
def program_files(): return [pathlib.Path(os.environ.get(k, d)) for k, d in (('ProgramFiles', r'C:\Program Files'), ('ProgramFiles(x86)', r'C:\Program Files (x86)'))]


def spirv_cross():
    p = os.environ.get('SPIRV_CROSS') or shutil.which('spirv-cross')
    if p and pathlib.Path(p).is_file(): return pathlib.Path(p)
    for root in (pathlib.Path('C:/VulkanSDK'),) + tuple(pathlib.Path(v) for v in (os.environ.get('VULKAN_SDK'),) if v):
        hits = sorted(root.glob('*/Bin/spirv-cross.exe')) + sorted(root.glob('Bin/spirv-cross.exe'))
        if hits: return hits[-1]
    return None


def ios_sdk():
    p = os.environ.get('NXW_IOS_SDK')
    if p and pathlib.Path(p).is_dir(): return pathlib.Path(p)
    hits = sorted((NX / 'win/sdk/out').glob('**/iPhoneOS*.sdk')) if (NX / 'win/sdk/out').is_dir() else []
    return hits[-1] if hits else None


def metal_tools():
    for pf in program_files():
        hits = sorted(pf.glob('Metal Developer Tools*'))
        if hits: return hits[-1]
    return None


def llvm_tool(name):
    p = shutil.which(name)
    if p: return pathlib.Path(p)
    for pf in program_files():
        q = pf / 'LLVM/bin' / f'{name}.exe'
        if q.is_file(): return q
    return None


# ---- Windows: the Windows game's own tools (the recompiler and runtime from windows/: nxanalyze, nxlift, nxrt; lld-link; SPIRV-Tools for nxshader)
WIN_KIT_FILES = ('nxanalyze.exe', 'nxlift.exe', 'nxrt.exe', 'SDL3.dll', 'nxrt.lib', 'lld-link.exe', 'spirv-opt.exe', 'spirv-val.exe', 'hle_inventory.tsv',
                 'play.ps1', 'nxshader/nxshader.py', 'nxshader/dmsh_abi.h')


def win_tools():
    """{file: path} of the Windows game's tools, plus 'dir' and 'missing': the download's kit (windows\\bin, or NXW_TOOLS=<a folder laid out the same way),
    else a source checkout's own build (windows\\build\\Release, with lld-link from LLVM for Windows and SPIRV-Tools from the Vulkan SDK)."""
    kit = pathlib.Path(os.environ['NXW_TOOLS']) if os.environ.get('NXW_TOOLS') else WIN_KIT
    if (kit / 'nxrt.exe').is_file():
        t = {n: kit / n for n in WIN_KIT_FILES}; t['dir'] = kit
        t['gamecontrollerdb.txt'] = kit / 'gamecontrollerdb.txt'   # SDL_GameControllerDB's controller mappings: copied beside nxrt.exe when the kit has them
        t['pokopia-launch.exe'] = kit / 'pokopia-launch.exe'       # the launcher as a program: out\Play Pokopia <version>.exe when the kit has it
    else:
        src = HERE / 'windows'; rel = src / 'build' / 'Release'
        t = {n: rel / 'bin' / n for n in ('nxanalyze.exe', 'nxlift.exe', 'nxrt.exe', 'SDL3.dll')}
        t.update({'nxrt.lib': rel / 'lib' / 'nxrt.lib', 'hle_inventory.tsv': src / 'src/nxrt/gen/out/hle_inventory.tsv', 'play.ps1': src / 'scripts/play.ps1',
                  'nxshader/nxshader.py': src / 'src/nxshader/nxshader.py', 'nxshader/dmsh_abi.h': src / 'src/nxrt/gfx/dmsh_abi.h',
                  'lld-link.exe': llvm_tool('lld-link'), 'dir': rel / 'bin'})
        t['gamecontrollerdb.txt'] = src / 'src/nxrt/front/third_party/SDL_GameControllerDB/gamecontrollerdb.txt'
        t['pokopia-launch.exe'] = rel / 'bin' / 'pokopia-launch.exe'
        vk = [pathlib.Path(v) / 'Bin' for v in (os.environ.get('VULKAN_SDK'),) if v] + sorted(pathlib.Path('C:/VulkanSDK').glob('*/Bin'))[-1:]
        for n in ('spirv-opt.exe', 'spirv-val.exe'): t[n] = next((d / n for d in vk if (d / n).is_file()), None)
    t['missing'] = [n for n in WIN_KIT_FILES if not (t.get(n) and pathlib.Path(t[n]).is_file())]
    return t


def fingerprint(*parts):
    h = hashlib.sha256()
    for p in parts:
        if isinstance(p, pathlib.Path):
            st = p.stat() if p.exists() else None
            h.update(f'{p}:{st.st_size if st else -1}:{int(st.st_mtime) if st else 0}\n'.encode())
        else: h.update(repr(p).encode() + b'\n')
    return h.hexdigest()[:16]


def digest(p):
    """A file's content as a fingerprint part: for the Windows tools, which an unzipped download gives new times without changing them"""
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''): h.update(chunk)
    return f'{pathlib.Path(p).name}:{h.hexdigest()[:16]}'


class Build:
    def __init__(self, out):
        self.out = pathlib.Path(out).resolve()
        self.logs = self.out / 'logs'; self.stamps = self.out / 'stamps'
        self.extract = self.out / 'extract'; self.exefs = self.extract / 'ExeFS'; self.romfs_img = self.extract / 'RomFS.img'
        self.patched = self.out / 'patched'; self.host = self.out / 'host' / 'dmhost'; self.font = self.out / 'font'
        self.profile_root = self.out / 'profile'; self.profile = self.profile_root / TITLE
        self.shader = self.out / 'shader'; self.corpus = self.shader / 'corpus'; self.ctl = self.shader / 'corpus-ctl'
        self.maxwell = self.shader / 'maxwell'; self.pkg = self.shader / 'pkg'; self.tables = self.shader / 'tables'
        self.cache = self.shader / 'cache'; self.ios = self.shader / 'ios'
        self.outputs = self.out / 'out'; self.rundir = self.out / 'run'; self.phone = self.out / 'phone'
        self.win = self.out / 'windows'   # the Windows game: program.db, game.dll, the Vulkan shaders, the runtime, play.json
        self.icon = self.out / 'icon.png'   # the apps' icon, made by nxs2.py icon from your own game; none = the blank default
        vf = self.out / 'version.txt'   # the game version the apps carry, from the dump's build line (build writes it, refresh reads it)
        self.version = vf.read_text().strip() if vf.is_file() else '1.0.5'
        self.game = read_game_tsv(PROFILE_SRC / 'game.tsv')
        self.modules = self.game['modules'][0].split()
        for d in (self.logs, self.stamps): d.mkdir(parents=True, exist_ok=True)

    def set_version(self, disp):
        self.version = disp if disp and disp != '?' else '1.0.5'   # v327680's line has no display version: the apps have called it 1.0.5 since the first builds
        self.out.mkdir(parents=True, exist_ok=True); (self.out / 'version.txt').write_text(self.version + '\n')

    # ---- stamps: a stage is skipped when its inputs are as at the last run
    def fresh(self, name, key):
        f = self.stamps / f'{name}.json'
        return f.is_file() and json.loads(f.read_text()).get('key') == key

    def stamp(self, name, key): (self.stamps / f'{name}.json').write_text(json.dumps({'key': key, 'time': time.strftime('%Y-%m-%d %H:%M:%S')}) + '\n')

    def env(self, **more):
        e = {'DM_GAMES_DIR': str(self.profile_root), 'DM_GAMES_FIRST': '1', 'DM_GAME': TITLE, 'GAME': TITLE, 'DM_ALLOW_OTHER_METAL': '1', 'NXNATIVE': str(NX),
             'AMPERE_LOWER': str(AMPERE), 'PYTHONDONTWRITEBYTECODE': '1', 'PYTHONUTF8': '1',
             'J': str(JOBS)}   # every worker pool of the build: the shader chain (dmsh_build.py), the lowering, the iPhone app compile on Windows
        if WIN: e.update(windows_env())
        else:
            d = dotnet_root()
            if d: e['DOTNET_ROOT'] = str(d); e['PATH'] = f'{d}:{os.environ.get("PATH", "")}'
        e.update(more); return e

    def corpus_dirs(self): return [d for d in (self.corpus / n for n in ('rdb', 'main-embedded', 'linkdata', 'dumps')) if d.is_dir()]

    def chain_version(self):
        r = subprocess.run([PY, NX / 'dm/gfx/tools/dmsh_chain.py'], capture_output=True, text=True, cwd=NX / 'dm/gfx/tools')
        if r.returncode or not r.stdout.strip(): raise Fail(f'dmsh_chain.py gave no chain version: {r.stderr.strip()[-300:]}')
        return r.stdout.strip().splitlines()[-1]

    def lib(self): return self.cache / f'b-{self.chain_version()}'


def read_game_tsv(path):
    g = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.startswith('#'): continue
        k, _, v = line.partition('\t'); g.setdefault(k, []).append(v)
    return g


def build_id(nso):
    with open(nso, 'rb') as f:
        f.seek(0x40); return f.read(16).hex()


def gentle_jobs():
    """How many worker processes the build runs at once by default. Shader compiles keep every worker at 100 %, for half an hour or more. On a Mac with Apple
    silicon that is fine at all cores. On a PC it is a long all-core load that some machines do not take: 13th and 14th generation Intel Core CPUs without
    Intel's 2024 microcode fix (BIOS update) crash under it. The default leaves most of a PC's cores alone; --full-speed or -j N uses more."""
    n = os.cpu_count() or 4
    return n if sys.platform == 'darwin' else max(2, min(8, n // 4))


def lower_priority():
    """The whole build at low priority (its child processes inherit it), so the machine stays responsive and the build takes only the time other work leaves."""
    try:
        if WIN:
            import ctypes
            k = ctypes.windll.kernel32
            k.GetCurrentProcess.restype = ctypes.c_void_p   # a pseudo-handle (-1): as a 32-bit int it would be truncated and the call would fail
            k.SetPriorityClass.argtypes = (ctypes.c_void_p, ctypes.c_uint32)
            k.SetPriorityClass(k.GetCurrentProcess(), 0x00004000)   # BELOW_NORMAL_PRIORITY_CLASS: the children inherit it
        else:
            os.nice(10)
    except Exception: pass


def windows_env():
    """What every tool started on Windows needs: the fork's shim on PYTHONPATH (it maps xcrun to Metal Developer Tools for Windows, the translator to
    `dotnet`, build_ipa.sh to win/build_app.py), the native SPIRV-Cross, the iPhoneOS SDK, and the shader workers the fork recommends."""
    e = {'PYTHONPATH': os.pathsep.join([str(NX / 'win/pyshim')] + [p for p in os.environ.get('PYTHONPATH', '').split(os.pathsep) if p])}
    sc = spirv_cross()
    if sc: e['SPIRV_CROSS'] = str(sc)
    sdk = ios_sdk()
    if sdk: e['NXW_IOS_SDK'] = str(sdk)
    llvm = llvm_tool('clang')
    if llvm: e['PATH'] = f'{llvm.parent}{os.pathsep}{os.environ.get("PATH", "")}'
    return e


# ---------------------------------------------------------------- environment
VENV = pathlib.Path.home() / '.pokopia-native' / 'venv'   # the builder's own Python environment on a Mac (the app's launcher looks there first)


def externally_managed():
    """A Python that refuses pip installs (PEP 668: Homebrew's, a Linux distribution's); a virtual environment never is."""
    import sysconfig
    return sys.prefix == sys.base_prefix and pathlib.Path(sysconfig.get_path('stdlib'), 'EXTERNALLY-MANAGED').is_file()


def metal_hint(sdk, selected, said):
    """Why `xcrun -sdk <sdk> metal` failed, in one line: the usual cause is the Command Line Tools selected instead of Xcode, which installing the
    Metal toolchain in Xcode does not change. selected is `xcode-select -p`, said what xcrun printed."""
    said = ' '.join(said.split()); low = said.lower()
    apps = pathlib.Path('/Applications')
    xcode = apps / 'Xcode.app' if (apps / 'Xcode.app').is_dir() else next(iter(sorted(apps.glob('Xcode*.app'))), None)
    if xcode is None:
        return 'install Xcode (App Store or developer.apple.com) and open it once, then Xcode -> Settings -> Components: install the Metal toolchain'
    if '/commandlinetools' in selected.lower() or not selected:
        return (f'Terminal uses Apple\'s Command Line Tools, which have no Metal compiler; switch it to Xcode (asks for your password), '
                f'in Terminal: sudo xcode-select -s {shlex.quote(str(xcode / "Contents" / "Developer"))}')
    if 'license' in low: return 'accept the Xcode license, in Terminal: sudo xcodebuild -license accept'
    if 'runfirstlaunch' in low or 'first launch' in low: return 'finish Xcode\'s setup, in Terminal: sudo xcodebuild -runFirstLaunch'
    if sdk == 'iphoneos' and ('cannot be located' in low or 'sdk "iphoneos"' in low):
        return 'Xcode -> Settings -> Components: install the iOS platform'
    if 'metal toolchain' in low or 'metaltoolchain' in low or 'unable to find utility' in low:
        return 'Xcode -> Settings -> Components: install the Metal toolchain, or in Terminal: xcodebuild -downloadComponent MetalToolchain'
    return ('Xcode -> Settings -> Components: install the Metal toolchain' + (' and the iOS platform' if sdk == 'iphoneos' else '')
            + (f' (xcrun said: {said[:160]})' if said else ''))


def doctor(need_ios=True, quiet=False, need_windows=True):
    """What this machine has of what the build needs. Returns the list of problems (empty = ready). On Windows need_ios is the iPhone IPA's toolchain
    and need_windows the Windows game's tools."""
    bad = []
    def ok(name, cond, hint):
        if not quiet: print(f'  {"ok     " if cond else "MISSING"}  {name}' + ('' if cond else f'   -> {hint}'))
        if not cond: bad.append(name)
    if not quiet: print(f'pokopia-native {TOOL_VERSION}')
    if WIN: return doctor_windows(ok, bad, need_ios, need_windows)
    ok('macOS on Apple silicon', sys.platform == 'darwin' and platform.machine() == 'arm64', 'the host runs the game\'s arm64 code natively: an Apple silicon Mac is required')
    ok(f'Python 3.12+ (running {platform.python_version()})', sys.version_info >= (3, 12), 'brew install python@3.12, then run nxs2.py with python3.12')
    managed = externally_managed(); missing = 0
    venv = shlex.quote(str(VENV))   # a managed Python's fix: the builder's own environment, made and filled in one go (the window shows only this line)
    venv_fix = f'in Terminal: {shlex.quote(sys.executable)} -m venv {venv} && {venv}/bin/python -m pip install "capstone>=5,<6" lz4 fonttools pillow'
    for mod, pip in (('capstone', 'capstone>=5,<6'), ('lz4', 'lz4'), ('fontTools', 'fonttools')):
        try: __import__(mod); have = True
        except ImportError: have = False; missing += 1
        ok(f'python module {mod}', have, venv_fix if managed else f'{PY} -m pip install "{pip}"')
    try:
        import capstone; ok('capstone 5.x', capstone.__version__.startswith('5.'), venv_fix if managed else f'{PY} -m pip install "capstone>=5,<6"')
    except ImportError: pass
    if missing and managed and not quiet:
        print(f'  This Python ({sys.executable}) is managed by Homebrew or the system, which keeps pip packages out of it.\n'
              f'  The builder gets its own environment instead, once, in Terminal:\n'
              f'    {sys.executable} -m venv {VENV}\n'
              f'    {VENV}/bin/python -m pip install "capstone>=5,<6" lz4 fonttools pillow\n'
              f'  then open Pokopia Native Builder again (it uses that environment by itself), or run nxs2.py with {VENV}/bin/python.')
    xc = subprocess.run(['xcode-select', '-p'], capture_output=True, text=True)
    ok('Xcode command line tools (xcode-select -p)', xc.returncode == 0 and xc.stdout.strip() != '', 'install Xcode from developer.apple.com, open it once, then: sudo xcode-select -s /Applications/Xcode.app')
    ok('clang', which('clang') is not None, 'comes with Xcode\'s command line tools')
    ok('make', which('make') is not None, 'comes with Xcode\'s command line tools')
    for sdk in ('macosx',) + (('iphoneos',) if need_ios else ()):
        r = subprocess.run(['xcrun', '-sdk', sdk, 'metal', '--version'], capture_output=True, text=True)
        ok(f'Metal compiler for {sdk}', r.returncode == 0, '' if r.returncode == 0 else metal_hint(sdk, xc.stdout.strip(), r.stdout + ' ' + r.stderr))
    d = dotnet_root()
    have_rt = False
    if d:
        r = subprocess.run([str(d / 'dotnet'), '--list-runtimes'], capture_output=True, text=True)
        have_rt = any(l.startswith('Microsoft.NETCore.App 10.') for l in r.stdout.splitlines())
    ok('.NET 10 runtime (the shader translator)', have_rt, 'brew install --cask dotnet-sdk   (or the .NET 10 runtime from dotnet.microsoft.com)')
    if need_ios: ok('xcodegen (the iPhone build)', which('xcodegen', '~/.local/bin/xcodegen') is not None, 'brew install xcodegen')
    ok('git', which('git') is not None, 'comes with Xcode\'s command line tools')
    std = pathlib.Path('/System/Library/Fonts/Supplemental/Arial Unicode.ttf')
    ok('a standard font on this Mac (Arial Unicode)', std.is_file(), 'STD_FONT=<a Unicode TTF> names another font for the game\'s text')
    return bad


def intel_microcode_warning():
    """13th and 14th generation Intel Core CPUs crash under long heavy loads until the BIOS carries Intel's 2024 microcode fix (0x12B or newer); the shader
    compile is such a load. A warning, not a requirement: the gentle default load keeps the risk low, a BIOS update removes it."""
    try:
        import winreg
        k = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'HARDWARE\DESCRIPTION\System\CentralProcessor\0')
        name = winreg.QueryValueEx(k, 'ProcessorNameString')[0].strip()
        raw = winreg.QueryValueEx(k, 'Update Revision')[0]
    except Exception: return None
    if not re.search(r'\b1[34]th Gen Intel', name): return None
    rev = int.from_bytes(raw[4:8] if len(raw) >= 8 else raw[:4], 'little')
    if rev >= 0x12B: return None
    return (f'{name} runs microcode {rev:#x}: these CPUs need Intel\'s 0x12B fix or newer (a BIOS update from your motherboard maker), '
            'or long full loads can crash Windows. The build\'s default load is gentle; avoid --full-speed until the BIOS is updated')


def doctor_windows(ok, bad, need_ios=True, need_windows=True):
    """Windows builds the iPhone IPA (the toolchain nxnative's Windows port documents) and the Windows game (our own tools in windows\\bin)."""
    w = intel_microcode_warning()
    if w: print(f'  WARNING  {w}')
    ok('Windows on x64', platform.machine().lower() in ('amd64', 'x86_64'), 'the Windows tools are x64 builds')
    ok(f'Python 3.12+ (running {platform.python_version()})', sys.version_info >= (3, 12), 'https://www.python.org/downloads/ (tick "Add python.exe to PATH")')
    for mod, pip in ((('capstone', 'capstone>=5,<6'),) if need_ios else ()) + (('lz4', 'lz4'), ('fontTools', 'fonttools'), ('PIL', 'pillow')):
        try: __import__(mod); have = True
        except ImportError: have = False
        hint = f'{PY} -m pip install "{pip}"'
        if mod == 'lz4' and sys.version_info >= (3, 15):   # no lz4 wheel for 3.15 yet: pip would compile it and asks for the C++ Build Tools
            hint = (f'lz4 has no ready-made package for Python {platform.python_version()} yet. Install Python 3.14 from python.org beside it, '
                    'run py -3.14 -m pip install lz4 fonttools pillow, then start Pokopia Native Builder again: it picks 3.14')
        ok(f'python module {mod}', have, hint)
    if need_ios:
        try:
            import capstone; ok('capstone 5.x', capstone.__version__.startswith('5.'), f'{PY} -m pip install "capstone>=5,<6"')
        except ImportError: pass
    try: import tkinter; ok('Tk (the window)', True, '')
    except ImportError: ok('Tk (the window)', False, 'reinstall Python from python.org with "tcl/tk and IDLE" ticked (the command line works without it)')
    dn = shutil.which('dotnet'); have_rt = False
    if dn:
        r = subprocess.run([dn, '--list-runtimes'], capture_output=True, text=True)
        have_rt = any(l.startswith('Microsoft.NETCore.App 10.') for l in r.stdout.splitlines())
    ok('.NET 10 runtime (the shader translator)', have_rt, 'https://dotnet.microsoft.com/download/dotnet/10.0 (the .NET Runtime is enough)')
    if need_ios:
        ok('LLVM for Windows: clang', llvm_tool('clang') is not None, 'https://github.com/llvm/llvm-project/releases (LLVM-<version>-win64.exe)')
        ok('LLVM for Windows: ld64.lld', llvm_tool('ld64.lld') is not None, 'comes with LLVM for Windows')
        mt = metal_tools()   # 4.x: ios/bin and macos/bin toolchains; 5.x and 6.x: one bin/ for every target
        ok('Metal Developer Tools for Windows (metal.exe)', mt is not None and any(mt.glob(p) for p in ('bin/metal.exe', 'ios/bin/metal.exe', 'ios/*/bin/metal.exe')),
           'https://developer.apple.com/download/all/?q=Metal%20Developer%20Tools (sign in with an Apple ID)')
        sdk = ios_sdk()
        ok(f'iPhoneOS SDK{f" ({sdk.name})" if sdk else ""}', sdk is not None, 'after setup: python nxnative\\win\\sdk\\extract_xip.py <Xcode.xip> (the SDK is taken from Xcode, downloaded from developer.apple.com), or NXW_IOS_SDK=<path>')
        ok('SPIRV-Cross (spirv-cross.exe)', spirv_cross() is not None, 'the Vulkan SDK from https://vulkan.lunarg.com (its Bin folder has it), or SPIRV_CROSS=<path>')
    ok('git', shutil.which('git') is not None, 'https://git-scm.com/download/win')
    fonts = pathlib.Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts'
    ok('a standard font (Arial Unicode or Arial)', any((fonts / n).is_file() for n in ('ARIALUNI.TTF', 'arial.ttf')) or bool(os.environ.get('STD_FONT')), 'STD_FONT=<a Unicode TTF>')
    if need_ios:
        ok('a bold font for the button icons (Arial Bold)', any((fonts / n).is_file() for n in ('ARLRDBD.TTF', 'arialbd.ttf')) or bool(os.environ.get('LETTER_FONT')), 'LETTER_FONT=<a bold TTF>')
    if need_windows:
        ok('a CPU with AVX2 and FMA3 (the recompiled game needs them)', cpu_avx2(), 'Intel Haswell (2013) or AMD Excavator (2015) and newer')
        sysdir = pathlib.Path(os.environ.get('WINDIR', r'C:\Windows')) / 'System32'
        ok('Vulkan (vulkan-1.dll, installed with the graphics driver)', (sysdir / 'vulkan-1.dll').is_file(),
           'install the current driver for your graphics card (NVIDIA, AMD or Intel); the game needs a Vulkan 1.3 GPU')
        t = win_tools()
        ok(f'the Windows game tools ({t.get("dir") or "windows\\bin"})', not t.get('missing'),
           f'missing {", ".join(t.get("missing", []))}: they come with the "for Windows PCs" download (windows\\bin); a source checkout builds them with '
           'windows\\scripts\\build.cmd, or NXW_TOOLS=<folder> names them')
    return bad


def cpu_avx2():
    """x86-64-v3, the recompiled code's minimum: AVX2 (PF_AVX2_INSTRUCTIONS_AVAILABLE); every CPU with AVX2 that Windows runs on has FMA3 too"""
    try:
        import ctypes
        return bool(ctypes.windll.kernel32.IsProcessorFeaturePresent(40))
    except Exception: return False


def patch_adds(patch):
    """The files a patch creates (its --- /dev/null entries)."""
    lines = patch.read_text(encoding='utf-8', errors='replace').splitlines()
    return [b[6:] for a, b in zip(lines, lines[1:]) if a == '--- /dev/null' and b.startswith('+++ b/')]


def check_writable(folder, what):
    """Windows Security's Controlled folder access (ransomware protection) and some antivirus programs stop Python writing in Documents,
    Desktop, Downloads and Pictures; Python then reports the new file as missing (Errno 2). Said before a step fails halfway."""
    folder = pathlib.Path(folder)
    try:
        folder.mkdir(parents=True, exist_ok=True)
        t = folder / f'.nxs2-write-test-{os.getpid()}'; t.write_text('ok'); t.unlink()
    except OSError as e:
        hint = ('Windows Security\'s "Controlled folder access" or an antivirus is probably blocking Python there. Move the folder somewhere '
                'outside Documents, Desktop, Downloads and Pictures (for example C:\\Pokopia Native) and run it from there, or allow python.exe in '
                'Windows Security > Virus & threat protection > Ransomware protection > Allow an app through Controlled folder access.'
                if WIN else 'check that the folder is yours and not read-only.')
        raise Fail(f'cannot write in {what} ({folder}: {e.strerror or e}). {hint}')


def unblock_download():
    """Windows marks every file Explorer unzips from a downloaded zip as coming from the internet, and the launcher's Start-Process then refuses the game's
    runtime ("The operation was canceled by the user"). The download's own files lose the mark; what setup fetches or the build makes has none."""
    if not WIN: return
    n = 0
    for root, dirs, files in os.walk(HERE):
        dirs[:] = [d for d in dirs if d not in ('.git', 'nxnative', 'nxnativewindows-src', 'dist', '__pycache__')]
        for f in files:
            try: os.remove(os.path.join(root, f) + ':Zone.Identifier'); n += 1
            except OSError: pass
    if n: say(f'{n} downloaded files unblocked (Windows had marked them as from the internet)')


# nxnative's own tools for encrypted games, and the docs and tests about them: its NSP extractor and NSP-to-app scripts (they take console
# keys), its Unreal Engine pak reader (it finds a game's AES key in the game's code and decrypts the pak index), and the Windows port's NSP
# driver, window and guides. Pokopia Native runs none of them, so every setup removes them from its copy of nxnative (and from the cached
# Windows port) and puts a stand-in where the pak reader was. None of them is in the shader chain's version (dmsh_chain.py FILES), so no
# build's cache changes.
NX_STRIP = ('dm/tools/nx_extract', 'scripts', 'docs', 'README.md', 'AGENTS.md', 'dm/tools/tests', 'dm/tools/fuzz',
            'win/build_ipa_windows.py', 'win/gui.pyw', 'win/README.md', 'win/TUTORIAL.md', 'win/LINUX.md')
UEPAK_STUB = ("# Pokopia Native's copy of nxnative: the Unreal Engine pak reader that was here (it decrypted pak indexes with a key found in\n"
              "# the game's code) is removed. Pokopia is not an Unreal Engine game; this stand-in only lets the modules that import it load.\n"
              "def unreal_paks(*a, **k): return []\n"
              "def __getattr__(name): raise AttributeError(f'uepak.{name}: Unreal Engine pak support is removed from this copy of nxnative')\n")

def strip_nxnative():
    """NX_STRIP removed, the pak reader replaced by UEPAK_STUB, and the AES helper taken out of the Windows port's Python shim. Safe to run any
    number of times: setup's resets bring the files back, and this removes them again."""
    fork = HERE / 'nxnativewindows-src'
    for root in (NX, fork):
        for rel in NX_STRIP:
            if root == fork and not rel.startswith('win/'): continue
            p = root / rel
            if p.is_dir(): shutil.rmtree(p)
            elif p.exists(): p.unlink()
    ue = NX / 'dm/gfx/tools/uepak.py'
    if ue.exists() and ue.read_bytes() != UEPAK_STUB.encode(): ue.write_bytes(UEPAK_STUB.encode())
    for shim in (NX / 'win/pyshim/sitecustomize.py', fork / 'win/pyshim/sitecustomize.py'):
        if not shim.is_file(): continue
        s = shim.read_text(encoding='utf-8')
        s2 = re.sub(r"    class _CCCrypt:\n.*?    class _LibSystem:\n        CCCrypt = _CCCrypt\(\)\n",
                    "    class _LibSystem(_NoLib): pass   # Pokopia Native's copy: no CommonCrypto (only the removed Unreal pak reader used it)\n", s, flags=re.S)
        if s2 != s: shim.write_bytes(s2.encode('utf-8'))

def setup(args):
    """nxnative at the pinned commit with the Switch 2 patch and the rebuilt translator, and ampere-lower built."""
    unblock_download()
    bad = doctor(need_ios=not args.no_ios, need_windows=False)
    if bad: raise Fail('install the missing tools above, then run setup again')
    check_writable(HERE, 'the pokopia-native folder')
    if not (NX / '.git').is_dir():
        say(f'fetching nxnative ({UPSTREAM} at {UPSTREAM_COMMIT}) into {NX}')
        run(['git', 'clone', '--filter=blob:none', '--no-checkout', UPSTREAM, NX], HERE / 'setup-clone.log')
    run(['git', 'config', 'core.autocrlf', 'false'], HERE / 'setup-config.log', cwd=NX)   # the sources as on a Mac: the chain version hashes them, and the patches expect LF
    def head(): return subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True, cwd=NX).stdout.strip()
    marker = NX / 'dm/gfx/nvn/.switch2-patch'
    patch = HERE / 'host/nxnative-switch2.patch'
    want = fingerprint(patch)
    if not head().startswith(UPSTREAM_COMMIT[:7]):
        say(f'checking out nxnative {UPSTREAM_COMMIT} (was {head() or "nothing"})')
        run(['git', 'checkout', '-q', '--', '.'], HERE / 'setup-reset.log', cwd=NX, ok=(0, 1))   # drop a patch applied to another commit
        for m in (marker, NX / 'dm/gfx/nvn/.windows-extras'):
            if m.exists(): m.unlink()
        run(['git', 'fetch', 'origin', UPSTREAM_COMMIT], HERE / 'setup-fetch.log', cwd=NX, ok=(0, 128))
        run(['git', 'checkout', '-q', UPSTREAM_COMMIT], HERE / 'setup-checkout.log', cwd=NX)
    if not (marker.is_file() and marker.read_text().strip() == want):
        dirty = subprocess.run(['git', 'status', '--porcelain', '--untracked-files=no'], capture_output=True, text=True, cwd=NX).stdout.strip()
        if dirty:
            say('resetting nxnative to the pinned commit before applying the patch')
            run(['git', 'checkout', '-q', '--', '.'], HERE / 'setup-reset.log', cwd=NX)
            if (NX / 'dm/gfx/nvn/.windows-extras').exists(): (NX / 'dm/gfx/nvn/.windows-extras').unlink()
        for f in patch_adds(patch):   # the patch's own new files (s2tex) from an earlier apply: the reset leaves them, and git apply will not create them over themselves
            if (NX / f).is_file(): (NX / f).unlink()
        say('applying the Switch 2 host patch')
        run(['git', 'apply', '--whitespace=nowarn', patch], HERE / 'setup-patch.log', cwd=NX)
        marker.write_text(want + '\n')
    bin_dst = NX / 'dm/gfx/tools/dmsh_xlate_bin'
    for f in sorted((HERE / ('host/windows/dmsh_xlate_bin' if WIN else 'host/dmsh_xlate_bin')).iterdir()):
        dst = bin_dst / f.name
        if not dst.is_file() or dst.read_bytes() != f.read_bytes():
            shutil.copy2(f, dst); say(f'translator: {f.name} installed')
    if not WIN: (bin_dst / 'dmsh_xlate').chmod(0o755)
    if WIN and not args.no_ios: setup_windows(want)
    elif WIN: say('the iPhone toolchain is skipped (--no-ios): this setup builds the Windows game; run setup without --no-ios for the iPhone app')
    elif not AMPERE.is_file() or AMPERE.stat().st_mtime < (HERE / 'ampere-lower/main.cpp').stat().st_mtime:
        say('building ampere-lower (the Switch 2 shader lowering)')
        run(['sh', HERE / 'ampere-lower/build.sh'], HERE / 'setup-ampere.log')
    strip_nxnative()   # nxnative's tools for encrypted games: never used here, so never kept
    say(f'setup done: nxnative {head()} + Switch 2 patch at {NX}, ampere-lower at {AMPERE}')


def setup_windows(patch_key):
    """The Windows side on top of the patched nxnative: the fork's win/ folder (LLVM + Metal Developer Tools build of the iPhone app, the tool shim), its three
    Windows fixes to nxnative, this project's changes to its app builder, and the tool lock rewritten for the tools on this PC (the native SPIRV-Cross and the
    Windows Metal compiler), followed by the chain header that carries the lock."""
    src = HERE / 'nxnativewindows-src'
    if not (src / '.git').is_dir():
        say(f'fetching nxnative\'s Windows port ({FORK} at {FORK_COMMIT[:7]}, its win/ folder)')
        if src.exists(): shutil.rmtree(src)
        run(['git', 'clone', '--filter=blob:none', '--no-checkout', '--sparse', FORK, src], HERE / 'setup-fork-clone.log')
        run(['git', 'config', 'core.autocrlf', 'false'], HERE / 'setup-fork-config.log', cwd=src)
        run(['git', 'sparse-checkout', 'set', 'win'], HERE / 'setup-fork-sparse.log', cwd=src)
    cur = subprocess.run(['git', 'rev-parse', 'HEAD'], capture_output=True, text=True, cwd=src).stdout.strip()
    if cur != FORK_COMMIT: run(['git', 'fetch', 'origin', FORK_COMMIT], HERE / 'setup-fork-fetch.log', cwd=src, ok=(0, 128))
    if cur != FORK_COMMIT or not (src / 'win/build_app.py').is_file():   # a --no-checkout clone has HEAD but no files yet
        run(['git', 'checkout', '-q', '-f', FORK_COMMIT], HERE / 'setup-fork-checkout.log', cwd=src)   # -f: strip_nxnative's removals in this cache
    if not (src / 'win/build_app.py').is_file(): raise Fail(f'the Windows port\'s win/ folder did not arrive in {src}; see {HERE / "setup-fork-checkout.log"}')
    if (NX / 'win').is_dir(): shutil.rmtree(NX / 'win')
    shutil.copytree(src / 'win', NX / 'win', ignore=shutil.ignore_patterns('__pycache__', 'build', 'sdk'))
    (NX / 'win/sdk').mkdir(exist_ok=True); shutil.copy2(src / 'win/sdk/extract_xip.py', NX / 'win/sdk/extract_xip.py')   # the SDK extractor; the SDK itself stays where the player put it (win/sdk/out)
    say('Windows port: win/ installed')
    extras = HERE / 'host/windows/fork-extras.patch'; marker = NX / 'dm/gfx/nvn/.windows-extras'   # beside the Switch 2 patch's marker; setup's reset removes both
    key = fingerprint(extras, patch_key)
    if not (marker.is_file() and marker.read_text().strip() == key):
        run(['git', 'apply', '--whitespace=nowarn', extras], HERE / 'setup-extras.log', cwd=NX); marker.write_text(key + '\n'); say('Windows fixes applied (RomFS image reads, process pool size, IPA packing)')
    run(['git', 'apply', '--whitespace=nowarn', '--directory=win', HERE / 'host/windows/build_app.patch'], HERE / 'setup-build-app.log', cwd=NX)
    say('app builder: Switch 2 changes applied')
    env = dict(os.environ, **windows_env())
    if not spirv_cross(): raise Fail('no spirv-cross.exe: install the Vulkan SDK (vulkan.lunarg.com) or set SPIRV_CROSS=<path to spirv-cross.exe>')
    say('tool lock: the translator, SPIRV-Cross and Metal compiler on this PC')
    run([PY, NX / 'dm/gfx/tools/tool_lock.py', '--update-lock'], HERE / 'setup-lock.log', env=env, cwd=NX / 'dm/gfx/tools')
    run([PY, NX / 'dm/gfx/tools/dmsh_chain.py', '--header'], HERE / 'setup-header.log', env=env, cwd=NX / 'dm/gfx/tools')
    fetch_rcodesign()   # signs the IPA on this PC (nxs2.py sign --p12 --profile)


# ---------------------------------------------------------------- the dump
def find_dump(args):
    """(exefs folder, romfs folder) of the update the recipe is for, under the folder given (the dump as a Switch 2 dumping tool writes it:
    <game>/updates/<version>/{exefs,romfs}, or a folder holding exefs/ and romfs/ directly), checked by the executable's build ID."""
    if args.exefs and args.romfs: cands = [(pathlib.Path(args.exefs), pathlib.Path(args.romfs))]
    else:
        d = pathlib.Path(args.dump).expanduser().resolve()
        roots = [d]
        if (d / 'updates').is_dir(): roots += sorted((p for p in (d / 'updates').iterdir() if p.is_dir()), reverse=True)
        if (d / 'base').is_dir(): roots.append(d / 'base')
        cands = []
        for r in roots:
            for ex, ro in (('exefs', 'romfs'), ('ExeFS', 'RomFS'), ('exefs', 'RomFS'), ('ExeFS', 'romfs')):
                if (r / ex / 'main').is_file() and (r / ro).is_dir() and (r / ex, r / ro) not in cands: cands.append((r / ex, r / ro))
    if not cands: raise Fail(f'no dump under {args.dump}: expected <folder>/updates/<version>/exefs/main and .../romfs/, or exefs/ and romfs/ in the folder itself')
    seen = []
    for ex, ro in cands:
        bid = build_id(ex / 'main'); seen.append(f'{ex}: main build {bid}')
        for ver, disp, want in builds():
            if bid != want['main']: continue
            for m, b in want.items():
                if not (ex / m).is_file(): raise Fail(f'{ex / m} is missing from the dump')
                if build_id(ex / m) != b: raise Fail(f'{ex / m} has build {build_id(ex / m)}, the recipe for {ver} expects {b}')
                short = nso_short(ex / m)
                if short: raise Fail(f'{ex / m} is {short} bytes shorter than its own header says: it was copied in text mode, which drops a byte from every '
                                     'CR LF pair in files without an extension. Copy the exefs folder again in binary mode (FileZilla: Transfer > Transfer type > Binary)')
            if not any(ro.rglob('*')): raise Fail(f'{ro} is empty')
            return ex, ro, f'{ver} ({disp})' if disp != '?' else ver, disp
    raise Fail(f'this recipe is for {NAME} ' + ', '.join(f'{v} (main build {w["main"]})' for v, _, w in builds()) + '; the dump holds:\n  ' +
               '\n  '.join(seen) + '\n(a base-game folder is not enough: the game\'s update is needed)')


def nup_unpack(path, out):
    """A .nup (a NUP container of an already-decrypted dump, nupread.py) unpacked into <build folder>/nup as a dump folder: each program's
    ExeFS (small), then the RomFS of the one this recipe is for, an update's own files over its base's (the format's layering). A second
    build of the same .nup unpacks nothing."""
    import nupread
    dest = out / 'nup'; stamp = dest / '.from'
    st = path.stat()
    try:
        with nupread.Nup(path) as n:
            key = f'{path}|{st.st_size}|{int(st.st_mtime)}|{n.uuid}'
            if stamp.is_file() and stamp.read_text() == key:
                say(f'.nup: {path.name} is unpacked already ({dest})'); return dest
            progs = sorted((p for p in n.packages if p['kind'] in ('base', 'update') and p['exefs'] is not None),
                           key=lambda p: (p['kind'] == 'update', p['version']), reverse=True)   # the newest update first, the base last
            if not progs: raise Fail(f'{path.name} holds no game program (no base or update package with an ExeFS)')
            if dest.exists():   # another .nup's, or an unpack that did not finish: only a folder this function made is removed
                if not (stamp.is_file() or (dest / '.unpacking').is_file()): raise Fail(f'{dest} exists and was not made by the builder: choose another build folder')
                shutil.rmtree(dest)
            dest.mkdir(parents=True); (dest / '.unpacking').write_text(key)
            known = {w['main'] for _, _, w in builds()}
            chosen, seen = None, []
            for p in progs:
                root = dest / ('base' if p['kind'] == 'base' else f'updates/{p["version"]}')
                for rel, fi in n.walk(p['exefs']): n.extract_file(fi, root / 'exefs' / rel)
                bid = build_id(root / 'exefs/main') if (root / 'exefs/main').is_file() else None
                seen.append(f'{p["kind"]} {p["version"]}: main build {bid}')
                if chosen is None and bid in known: chosen = (p, root)
            if not chosen:
                raise Fail(f'this recipe is for {NAME} ' + ', '.join(f'{v} (main build {w["main"]})' for v, _, w in builds()) + f'; {path.name} holds:\n  ' + '\n  '.join(seen))
            p, root = chosen
            base = next((q for q in n.packages if q['kind'] == 'base' and q['romfs'] is not None and q is not p), None)
            files = {}
            for tree in ([p] if p['romfs'] is not None else []) + ([base] if base else []):
                for rel, fi in n.walk(tree['romfs']): files.setdefault(rel, fi)   # the update's file shadows the base's
            if not files: raise Fail(f'{path.name}: the game program has no RomFS')
            need = sum(n.files[fi][2] for fi in files.values()); free = shutil.disk_usage(out).free
            if free < need + (2 << 30): raise Fail(f'unpacking {path.name} needs about {need / (1 << 30):.0f} GB free where the build folder is; {free / (1 << 30):.0f} GB are')
            say(f'.nup: unpacking {p["kind"]} {p["version"]} from {path.name}: {len(files)} RomFS files, {need / (1 << 30):.1f} GB')
            done = 0; step = need // 10 or 1
            for rel, fi in files.items():
                n.extract_file(fi, root / 'romfs' / rel)
                before, done = done, done + n.files[fi][2]
                if done // step != before // step: say(f'    {done * 100 // need}%')
    except nupread.NupError as e:
        raise Fail(str(e))
    stamp.write_text(key); (dest / '.unpacking').unlink()
    return dest


def nso_short(p):
    """How many bytes an NSO lacks against the segments its header lists (0 = complete)."""
    h = p.read_bytes()[:0x70]; flags = struct.unpack_from('<I', h, 0xC)[0]
    end = max(struct.unpack_from('<I', h, 0x10 + 0x10 * i)[0] + struct.unpack_from('<I', h, 0x60 + 4 * i if flags & (1 << i) else 0x18 + 0x10 * i)[0] for i in range(3))
    return max(0, end - p.stat().st_size)


def builds():
    """The game versions the profile knows: (version, display version, {module: build ID}) per build line."""
    out = []
    for line in read_game_tsv(PROFILE_SRC / 'game.tsv')['build']:
        ver, disp, *mods = line.split('\t')
        out.append((ver, disp, dict(m.split('=', 1) for m in mods if '=' in m)))
    return out


# ---------------------------------------------------------------- stages
def st_profile(b):
    """The game profile the tools read: game.tsv, the rewriter's patches.py, the entitlements and the empty shader tables (the package fills them later)."""
    b.profile.mkdir(parents=True, exist_ok=True)
    for name in ('game.tsv', 'patches.py', 'pokopia.entitlements', 'runtime_keys.tsv', 'shader_sources.tsv', 'shader_facts.seed.tsv'):
        src = PROFILE_SRC / name
        if src.is_file(): shutil.copyfile(src, b.profile / name)
    for name, text in EMPTY_TABLES.items():
        if not (b.profile / name).is_file(): (b.profile / name).write_text(text)


def st_extract(b, exefs, romfs):
    """The game as nxnative's tools read it: the ExeFS copied, a control record with the game's name, the RomFS packed into one image."""
    b.exefs.mkdir(parents=True, exist_ok=True)
    for f in sorted(exefs.iterdir()):
        if f.is_file() and (not (b.exefs / f.name).is_file() or (b.exefs / f.name).stat().st_size != f.stat().st_size): shutil.copy2(f, b.exefs / f.name)
    nacp = b.extract / 'control.nacp'
    data = make_nacp(NAME, int(TITLE, 16), b.version)
    if not nacp.is_file() or nacp.read_bytes() != data: nacp.write_bytes(data)
    say(f'RomFS image from {romfs} ({sum(1 for _ in romfs.rglob("*") if _.is_file())} files)')
    run([PY, NX / 'dm/tools/romfs_image.py', '--reuse', romfs, b.romfs_img], b.logs / 'romfs_image.log', env=b.env())
    if not b.romfs_img.is_file(): raise Fail('no RomFS image was written')


def make_nacp(name, title, version):   # a control record (NACP) with the game's name and version: Switch 2 dumps carry none, and the app build reads one
    import struct
    d = bytearray(0x4000)
    for lang in range(16):
        d[lang * 0x300:lang * 0x300 + len(name)] = name.encode(); d[lang * 0x300 + 0x200:lang * 0x300 + 0x207] = b'unknown'
    struct.pack_into('<Q', d, 0x3038, title); d[0x3060:0x3060 + len(version)] = version.encode()
    struct.pack_into('<Q', d, 0x3078, title); struct.pack_into('<Q', d, 0x30B0, title)
    return bytes(d)


def st_rewrite(b):
    """The game's code modules rewritten for Apple's kernel (nxnative's patch_nso.py), with their recipe tables."""
    key = fingerprint(*(b.exefs / m for m in b.modules), NX / 'dm/tools/patch_nso.py', PROFILE_SRC / 'game.tsv', PROFILE_SRC / 'patches.py')   # patches.py: the profile's gates
    if b.fresh('rewrite', key) and all((b.patched / f'{m}.patched.bin').is_file() for m in b.modules): return say('rewrite: up to date')
    b.patched.mkdir(parents=True, exist_ok=True)
    env = b.env(DM_REWRITE_J='1' if WIN else str(JOBS))   # the rewriter's worker pool forks (no fork on Windows): one worker runs the same code inline, about a minute
    for m in b.modules:
        say(f'rewriting {m}' + (' (one worker on Windows: a few minutes for main)' if WIN and m == 'main' else ''))
        run([PY, NX / 'dm/tools/patch_nso.py', b.exefs / m, b.patched, m], b.logs / f'rewrite_{m}.log', env=env, cwd=NX)
    b.stamp('rewrite', key)


def st_host(b, jobs):
    """The Mac host built for this game's profile (the profile is compiled in, so the binary is per game)."""
    srcs = [p for d in ('dm/host', 'dm/gfx', 'dm/gfx/nvn', 'dm/gfx/tex', 'dm/gfx/msl', 'dm/mac') for p in sorted((NX / d).iterdir())
            if p.is_file() and (p.suffix in ('.c', '.m', '.h', '.S', '.inc', '.metal') or p.name == 'Makefile')]   # the sources, not make's outputs
    key = fingerprint(*srcs, PROFILE_SRC / 'game.tsv')
    if b.fresh('host', key) and b.host.is_file(): return say('host: up to date')
    say('building the Mac host (dmhost)')
    run(['make', '-C', NX / 'dm/host', f'GAME={TITLE}', 'dmhost', f'-j{jobs}'], b.logs / 'host_make.log', env=b.env(), show=r'error:|\[RED\]')
    b.host.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(NX / 'dm/host/dmhost', b.host)
    b.stamp('host', key)


def st_font(b):
    """Stand-ins for the console's shared fonts, made from fonts on this Mac (nothing from the console): the standard font is a copy of the
    Mac's own Arial Unicode (STD_FONT=<ttf> names another), the button-icon font is generated by nxnative's make_ext_font.py."""
    b.font.mkdir(parents=True, exist_ok=True)
    std = pathlib.Path(os.environ.get('STD_FONT') or '/System/Library/Fonts/Supplemental/Arial Unicode.ttf')
    if not std.is_file(): raise Fail(f'no standard font at {std}: set STD_FONT=<a Unicode TTF on this Mac>')
    if not (b.font / 'standard.ttf').is_file(): shutil.copyfile(std, b.font / 'standard.ttf')
    if not (b.font / 'nintendo_ext.ttf').is_file():
        run([PY, NX / 'dm/tools/make_ext_font.py', b.font / 'nintendo_ext.ttf'], b.logs / 'make_ext_font.log', env=b.env())


def st_corpus(b, romfs, dumps):
    """The game's shaders as the lowering reads them: every shader container in the game's files (shader_sources.tsv says where, read straight from
    the dump's RomFS folder, read-only), and any shader dumps of earlier runs (--dumps). Writes <shader>/corpus/<set>/ and <shader>/corpus-ctl/."""
    sources = [l.split('\t') for l in (PROFILE_SRC / 'shader_sources.tsv').read_text().splitlines() if l.strip() and not l.startswith('#')]
    key = fingerprint(b.exefs / 'main', b.romfs_img, *sorted(TOOLS.glob('*.py')), sources)
    if not (b.fresh('corpus', key) and (b.ctl / 'hostkeys.tsv').is_file()):
        for d in (b.corpus, b.ctl):
            if d.is_dir(): shutil.rmtree(d)
        nso = b.shader / 'nso'; nso.mkdir(parents=True, exist_ok=True)
        say('reading the executable\'s data (nso_tool.py)')
        run([PY, TOOLS / 'nso_tool.py', b.exefs / 'main', '--out', nso], b.logs / 'nso_tool.log', env=b.env())
        jobs = []   # (label, corpus set, mode, args)
        for kind, *paths in sources:
            if kind == 'blob': jobs.append(('main', 'main-embedded', 'blob', [nso / 'main.rodata', 'main.rodata']))
            elif kind == 'linkdata': jobs.append(('linkdata', 'linkdata', 'linkdata', [romfs / p for p in paths]))
            elif kind == 'rdb': jobs.append((pathlib.Path(paths[0]).name, 'rdb', 'rdb', [romfs / paths[0]]))
        for label, cset, mode, args in jobs:
            for p in args:
                if isinstance(p, pathlib.Path) and not p.is_file(): raise Fail(f'shader source {p} is missing from the dump')
        say(f'extracting the shaders of {len(jobs)} sources')
        def extract(j):
            run([PY, TOOLS / 'glslc2_extract.py', b.corpus / j[1], j[2], *j[3]], b.logs / f'corpus_{j[0]}.log', env=b.env(), heartbeat=120); return j[0]
        with cf.ThreadPoolExecutor(max(1, min(4, os.cpu_count() or 2))) as ex:   # the extractions in parallel; the control scans one at a time (they append to one hostkeys.tsv)
            for label in ex.map(extract, jobs): say(f'  extracted {label}')
        for label, cset, mode, args in jobs:
            run([PY, TOOLS / 's2_ctl_scan.py', b.ctl, mode, *args], b.logs / f'ctl_{label}.log', env=b.env(), heartbeat=120)
        b.stamp('corpus', key)
    else: say('corpus: up to date')
    add_dumps(b, dumps)


def add_dumps(b, dumps):
    """Shader dumps recorded while the game ran (DM_NVN_SHDUMP folders: <key>.ctl + <key>.code) become corpus entries and host keys."""
    dirs = [pathlib.Path(d).expanduser() for d in dumps if pathlib.Path(d).expanduser().is_dir() and any(pathlib.Path(d).expanduser().glob('*.ctl'))]
    if not dirs: return
    say(f'adding shader dumps from {len(dirs)} folder(s)')
    run([PY, TOOLS / 's2_dumps.py', *dirs, '--corpus', b.corpus / 'dumps', '--ctl', b.ctl], b.logs / 's2_dumps.log', env=b.env(), show=r'new|added|programs')


def st_lower(b, jobs):
    """Every corpus program lowered to Maxwell with ampere-lower (incremental: programs lowered before are kept)."""
    say('lowering the shaders (ampere-lower)')
    run([PY, TOOLS / 'lower_corpus.py', b.maxwell, *b.corpus_dirs()], b.logs / 'lower.log', env=b.env(J=str(jobs)), show=r'^programs|already done|^done')


def st_package(b):
    """The shader package nxnative's chain reads, and the profile's shader tables (shader_map.tsv, shader_facts.tsv with the hand-made answers on top)."""
    say('packaging the shaders (s2_pkg.py)')
    for d in (b.pkg, b.tables):
        if d.is_dir(): shutil.rmtree(d)
    run([PY, TOOLS / 's2_pkg.py', b.ctl, b.maxwell, b.pkg, b.tables, *b.corpus_dirs()], b.logs / 's2_pkg.log', env=b.env(), show=r'records|keys|written|pkg')
    shutil.copyfile(b.tables / 'shader_map.tsv', b.profile / 'shader_map.tsv')
    if (b.tables / 'shader_pairs.tsv').is_file(): shutil.copyfile(b.tables / 'shader_pairs.tsv', b.profile / 'shader_pairs.tsv')
    facts = {}
    order = []
    header = []
    for line in (b.tables / 'shader_facts.tsv').read_text(encoding='utf-8').splitlines():
        if line.startswith('#'): header.append(line); continue
        if not line.strip(): continue
        k = line.split('\t')[0]
        if k not in facts: order.append(k)
        facts[k] = line
    seed = PROFILE_SRC / 'shader_facts.seed.tsv'
    n = 0
    if seed.is_file():
        for line in seed.read_text(encoding='utf-8').splitlines():
            if line.startswith('#') or not line.strip(): continue
            k = line.split('\t')[0]
            if k not in facts: order.append(k)
            facts[k] = line; n += 1
    (b.profile / 'shader_facts.tsv').write_text('\n'.join(header + [facts[k] for k in order]) + '\n', encoding='utf-8')
    say(f'tables: {len(order)} facts rows ({n} hand-made), shader map {sum(1 for l in (b.profile / "shader_map.tsv").read_text().splitlines() if l and not l.startswith("#")) - 1} entries')


def st_chain(b, ios):
    """nxnative's shader chain: the lowered programs to Metal libraries for the Mac (and the iPhone), in <shader>/cache/b-<chain>/ and <shader>/ios/b-<chain>/.
    Incremental through the chain's own cache; the first run compiles everything (around half an hour on an M-series Mac, more with the iOS libraries)."""
    say('Metal libraries (nxnative chain: translate, cross-compile, metal)' + (' for the iPhone' if WIN else ' for the Mac and the iPhone' if ios else ' for the Mac'))
    env = b.env(DMSH_S2_CS='1', DMSH_S2_CENSUS='1')
    if ios: env['IOSOUT'] = str(b.ios)
    if WIN: env['DMSH_TARGETS'] = 'ios'   # no Mac host here: the macOS libraries are not needed (the tables are still made from the Mac-side reflection)
    b.cache.mkdir(parents=True, exist_ok=True)
    cmd = [PY, NX / 'dm/gfx/tools/own_compilers.py', b.pkg, b.cache]
    try: run(cmd, b.logs / 'chain.log', env=env, cwd=NX / 'dm/gfx/tools', heartbeat=60, show=r'Metal cache|compiled|reused|\[RED\]|failed|success|lock')
    except Fail:
        # Python 3.12.0 on Windows once failed c1_guard.py with "internal error in regular expression engine" and passed on the next run: one retry, which reuses every library built
        if 'internal error in regular expression engine' not in (b.logs / 'chain.log').read_text(encoding='utf-8', errors='replace'): raise
        say('the shader step hit an intermittent Python regex error; running it once more (everything built so far is reused)')
        run(cmd, b.logs / 'chain.log', env=env, cwd=NX / 'dm/gfx/tools', heartbeat=60, show=r'Metal cache|compiled|reused|\[RED\]|failed|success|lock')
    lib = b.lib()
    if not (lib / 'inputs.tsv').is_file(): raise Fail(f'the chain left no library stamp at {lib}/inputs.tsv; see {b.logs / "chain.log"}')


def st_mac_app(b):
    """A self-contained Mac app (nxnative's pack_app.py): launcher + host + the game files (APFS clones) + shader libraries + tables."""
    app = b.outputs / f'{NAME}.app'; b.outputs.mkdir(parents=True, exist_ok=True)
    envf = b.out / 'app_env.txt'; envf.write_text(APP_ENV)
    settings = b.out / 'launcher.json'
    run([PY, NX / 'dm/games/settings.py', TITLE, b.patched, b.extract / 'control.nacp', settings], b.logs / 'settings.log',
        env=b.env(GFXAPI='nvn', SETTINGS_PLATFORM='mac', DM_LANG='en-US'))
    cmd = [PY, NX / 'dm/mac/pack_app.py', app, '--host', b.host, '--patched', b.patched, '--romfs', b.romfs_img, '--msl', b.lib(), '--tables', b.profile,
           '--font', b.font, '--title-id', TITLE, '--name', NAME, '--bundle-id', f'org.nxnative.{TITLE.lower()}', '--lang', 'en-US', '--sign', '-',
           '--version', b.version, '--category', b.game['app_category'][0], '--marker', b.game['romfs_marker'][0], '--env-file', envf, '--settings', settings]
    if b.icon.is_file(): cmd += ['--icon', b.icon]
    say('packing the Mac app')
    run(cmd, b.logs / 'pack_app.log', env=b.env(), show=r'^step:|^app:')
    if not (app / 'Contents/MacOS/dmhost').is_file(): raise Fail(f'no app at {app}')
    # the app's host records run-time shaders into shdump/ in its Application Support folder, which it does not create itself
    (pathlib.Path.home() / 'Library/Application Support/nxnative' / TITLE / 'shdump').mkdir(parents=True, exist_ok=True)
    say(f'Mac app: {app}')
    return app


def ipa_env(b):
    return b.env(W=str(b.out / 'ipa'), PATCHED=str(b.patched), ROMFS=str(b.romfs_img), MSL_IOS=str(b.ios), AOC='', DM_COPY_ROMFS='NO',
                 DM_ENTITLEMENTS=str(b.profile / 'pokopia.entitlements'), FONT_PY=PY, DM_SHOW_FPS='1', DM_PSO_GPUS='none', DM_ENV_FILE=str(b.out / 'phone_env.txt'),
                 DM_ICON=str(b.icon if b.icon.is_file() else b.out / 'no-icon'),   # build_ipa.sh otherwise looks for an icon.jpg beside the RomFS image
                 PATH=os.path.expanduser('~/.local/bin') + ':' + b.env().get('PATH', os.environ.get('PATH', '')))


def st_ipa(b):
    """An unsigned IPA (nxnative's build_ipa.sh): the app without the game files, which the phone takes from Documents/romfs.img; the executable
    carries the two kernel entitlements ad-hoc, so a re-signer that keeps entitlements can sign it for your own device."""
    (b.out / 'phone_env.txt').write_text(PHONE_ENV)
    b.outputs.mkdir(parents=True, exist_ok=True)
    out = b.outputs / f'{NAME.replace(" ", "")}-unsigned.ipa'
    say('building the IPA (unsigned)')
    run(['bash', NX / 'dm/ios/tools/build_ipa.sh', out], b.logs / 'build_ipa.log', env=ipa_env(b), show=r'^\[\d\d:\d\d:\d\d\]|FAIL|error:')
    if not out.is_file(): raise Fail(f'no IPA at {out}; see {b.logs / "build_ipa.log"}')
    say(f'IPA: {out} ({out.stat().st_size // (1 << 20)} MiB; romfs.img goes to the app\'s Documents on the phone, see docs/IPHONE.md)')
    return out


def st_ipa_windows(b):
    """The unsigned IPA on Windows: the fork's win/build_app.py (LLVM clang + ld64.lld with the iPhoneOS SDK, the host's shaders through Metal Developer Tools for
    Windows) with the same inputs as build_ipa.sh; the executable carries both kernel entitlements in an ad-hoc signature, the game files stay out (romfs.id instead)."""
    (b.out / 'phone_env.txt').write_text(PHONE_ENV)
    b.outputs.mkdir(parents=True, exist_ok=True)
    out = b.outputs / f'{NAME.replace(" ", "")}-unsigned.ipa'
    env = b.env(W=str(b.out / 'ipa'), PATCHED=str(b.patched), ROMFS=str(b.romfs_img), MSL_IOS=str(b.ios), AOC='', DM_ENV_FILE=str(b.out / 'phone_env.txt'), DM_SHOW_FPS='1',
                DM_PSO_GPUS='none', DM_LANG='en-US', DM_BUNDLE_ID=f'org.nxnative.{TITLE.lower()}', NXW_EXTENDED_VA='1', NXW_ROMFS_SEPARATE='1', NXW_ENTITLEMENTS='1',
                DM_ICON=str(b.icon if b.icon.is_file() else b.out / 'no-icon'))
    (b.out / 'ipa').mkdir(parents=True, exist_ok=True)
    for old in (out, out.with_name(out.name + '.idx')):   # pack_ipa.py reuses an existing IPA through its .idx with os.pread, which Windows lacks: pack it whole instead
        if old.exists(): old.unlink()
    say('building the IPA (unsigned; LLVM and Metal Developer Tools for Windows)')
    run([PY, NX / 'win/build_app.py', out], b.logs / 'build_ipa.log', env=env, show=r'^\[\d\d:\d\d:\d\d\]|FAIL|error')
    if not out.is_file(): raise Fail(f'no IPA at {out}; see {b.logs / "build_ipa.log"}')
    say(f'IPA: {out} ({out.stat().st_size // (1 << 20)} MiB; sign it with your own team, then romfs.img goes to the app\'s Documents: docs/IPHONE.md)')
    return out


def st_xcode(b, team):
    """An Xcode project for the game, to sign with your Apple developer team (nxs2.py sign, or Xcode itself)."""
    (b.out / 'phone_env.txt').write_text(PHONE_ENV)
    env = ipa_env(b); env['DM_TEAM_ID'] = team; env['DM_XCODE_PROJECT_DIR'] = str(b.out / 'xcode')
    say(f'preparing the Xcode project (team {team})')
    run(['bash', NX / 'dm/ios/tools/build_ipa.sh', '--prepare-xcode'], b.logs / 'prepare_xcode.log', env=env, show=r'^\[\d\d:\d\d:\d\d\]|FAIL|error:')
    proj = b.out / 'xcode' / 'DmIOS.xcodeproj'
    if not proj.is_dir(): raise Fail(f'no project at {proj}; see {b.logs / "prepare_xcode.log"}')
    say(f'Xcode project: {proj}')
    return proj


RCODESIGN_VERSION = '0.29.0'
RCODESIGN_ZIP = f'apple-codesign-{RCODESIGN_VERSION}-x86_64-pc-windows-msvc.zip'
RCODESIGN_URL = f'https://github.com/indygreg/apple-platform-rs/releases/download/apple-codesign%2F{RCODESIGN_VERSION}/{RCODESIGN_ZIP}'
RCODESIGN_SHA256 = '54bb500e2da7a8de02fcae0f331d1cac6e6d7173b4281042ff9c528ba3159aaa'   # the release's own .sha256


def rcodesign():
    """rcodesign (apple-codesign, MIT/Apache-2.0): RCODESIGN, on PATH, or the copy setup fetched on Windows into tools-bin/"""
    for p in (os.environ.get('RCODESIGN'), shutil.which('rcodesign'), HERE / 'tools-bin' / ('rcodesign.exe' if WIN else 'rcodesign')):
        if p and pathlib.Path(p).is_file(): return pathlib.Path(p)
    return None


def fetch_rcodesign():
    """Windows: the pinned rcodesign release into tools-bin/, checked against its SHA-256 (setup)"""
    if rcodesign(): return
    import hashlib, io, urllib.request, zipfile
    say(f'fetching rcodesign {RCODESIGN_VERSION} (signs the IPA with your own certificate on Windows)')
    data = urllib.request.urlopen(RCODESIGN_URL, timeout=120).read()
    if hashlib.sha256(data).hexdigest() != RCODESIGN_SHA256: raise Fail(f'rcodesign download does not match its published SHA-256: {RCODESIGN_URL}')
    z = zipfile.ZipFile(io.BytesIO(data)); exe = next(n for n in z.namelist() if n.endswith('/rcodesign.exe'))
    (HERE / 'tools-bin').mkdir(exist_ok=True); (HERE / 'tools-bin/rcodesign.exe').write_bytes(z.read(exe))
    lic = next((n for n in z.namelist() if n.endswith('/COPYING')), None)
    if lic: (HERE / 'tools-bin/rcodesign-COPYING.txt').write_bytes(z.read(lic))


def profile_plist(path):
    """The plist inside a provisioning profile (a CMS-signed blob; the plist is stored in it as is)"""
    import plistlib
    b = pathlib.Path(path).read_bytes(); i = b.find(b'<?xml'); j = b.find(b'</plist>', i)
    if i < 0 or j < 0: raise Fail(f'{path} is not a provisioning profile (.mobileprovision)')
    return plistlib.loads(b[i:j + len(b'</plist>')])


def sign_with_certificate(b, p12, profile, password_stdin=False):
    """A development IPA from the unsigned one without Xcode (Windows, or a Mac without it): your Apple Development certificate and its key as a .p12,
    and a development provisioning profile for org.nxnative.<title id> that lists your iPhone and grants Increased Memory Limit and Extended Virtual
    Addressing (an App ID with both capabilities on a paid team; the profile Xcode made for the Mac build is one). The profile goes into the app as
    embedded.mobileprovision, its entitlements are signed in by rcodesign, and the result is <build>/out/PokemonPokopia-signed.ipa"""
    import datetime, getpass, plistlib, tempfile, zipfile
    rc = rcodesign() or Fail('rcodesign not found: run nxs2.py setup (it fetches it on Windows), or set RCODESIGN=<path>')
    if isinstance(rc, Fail): raise rc
    src = b.outputs / f'{NAME.replace(" ", "")}-unsigned.ipa'
    if not src.is_file(): raise Fail(f'no unsigned IPA at {src}: build with --ipa first')
    prof = profile_plist(profile); ents = dict(prof.get('Entitlements', {})); team = (prof.get('TeamIdentifier') or ['?'])[0]
    with zipfile.ZipFile(src) as z:
        info = plistlib.loads(z.read('Payload/DmIOS.app/Info.plist'))
    bundle = info['CFBundleIdentifier']; appid = ents.get('application-identifier', '')
    if appid.endswith('*'): ents['application-identifier'] = f'{team}.{bundle}'   # a wildcard profile: this app's own identifier
    elif appid != f'{team}.{bundle}': raise Fail(f'the profile is for {appid}, the app is {team}.{bundle}: make a profile for the app\'s bundle ID')
    exp = prof.get('ExpirationDate')
    if isinstance(exp, datetime.datetime) and exp < datetime.datetime.now(): raise Fail(f'the profile "{prof.get("Name")}" expired on {exp:%Y-%m-%d}')
    for need in ('com.apple.developer.kernel.extended-virtual-addressing', 'com.apple.developer.kernel.increased-memory-limit'):
        if not ents.get(need): say(f'WARNING: the profile does not grant {need.split(".")[-1]}: the game will stop at start-up for lack of memory')
    say(f'signing for team {team}, profile "{prof.get("Name")}" ({len(prof.get("ProvisionedDevices", []))} device(s), expires {exp:%Y-%m-%d})' if exp else f'signing for team {team}')
    pw = sys.stdin.readline().rstrip('\r\n') if password_stdin else getpass.getpass(f'password of {pathlib.Path(p12).name}: ')
    work = b.out / 'sign-work'
    if work.exists(): shutil.rmtree(work)
    work.mkdir(parents=True)
    try:
        say('unpacking the unsigned IPA')
        with zipfile.ZipFile(src) as z: z.extractall(work)
        app = work / 'Payload/DmIOS.app'
        shutil.copyfile(profile, app / 'embedded.mobileprovision')
        (work / 'entitlements.plist').write_bytes(plistlib.dumps(ents))
        pwf = work / 'p12.pw'; pwf.write_text(pw, encoding='utf-8'); pw = None
        say('signing (rcodesign)')
        try:
            # scoped to the executable: rcodesign reads the text before the first colon as the scope (C:\ would make the drive letter one), and with the
            # scope "main" or none it keeps the entitlements of the executable's ad-hoc signature (the two kernel ones) instead of the profile's
            run([rc, 'sign', '--p12-file', p12, '--p12-password-file', pwf, '--entitlements-xml-file', f'{info.get("CFBundleExecutable", "DmIOS")}:{work / "entitlements.plist"}', app],
                b.logs / 'sign.log')
        finally:
            pwf.unlink(missing_ok=True)
        sig = subprocess.run([rc, 'print-signature-info', app / info.get('CFBundleExecutable', 'DmIOS')], capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
        lost = [k for k in ('application-identifier', 'com.apple.developer.kernel.extended-virtual-addressing', 'com.apple.developer.kernel.increased-memory-limit') if k not in sig]
        if lost: raise Fail(f'the signed app lacks the entitlements {", ".join(lost)} (iOS would refuse it); see {b.logs / "sign.log"}')
        out = b.outputs / f'{NAME.replace(" ", "")}-signed.ipa'
        say('packing the signed IPA')
        tmp = out.with_name(out.name + '.part')
        with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for p in sorted((work / 'Payload').rglob('*')):
                rel = p.relative_to(work).as_posix()
                if p.is_dir():
                    zi = zipfile.ZipInfo(rel + '/'); zi.external_attr = (0o40755 << 16) | 0x10; z.writestr(zi, b''); continue
                zi = zipfile.ZipInfo.from_file(p, rel)
                zi.external_attr = ((0o100755 if rel == 'Payload/DmIOS.app/DmIOS' else 0o100644) << 16)   # Windows has no x bit: the executable gets it here
                zi.compress_type = zipfile.ZIP_STORED if p.suffix in ('.metallib', '.bin', '.fdata', '.webm') else zipfile.ZIP_DEFLATED
                with open(p, 'rb') as f, z.open(zi, 'w', force_zip64=True) as w: shutil.copyfileobj(f, w, 16 << 20)
        os.replace(tmp, out)
        say(f'signed IPA: {out}  (install: pymobiledevice3 apps install "{out}"; the game files: docs/IPHONE.md)')
    finally:
        shutil.rmtree(work, ignore_errors=True)


def sign(args):
    """Archive and export a development IPA from the prepared Xcode project with automatic signing (your Apple ID signed into Xcode, the
    iPhone's UDID on the team). The first run should happen in Terminal on the Mac itself: Xcode may ask for the keychain.
    With --p12 and --profile: the unsigned IPA signed with your certificate and profile instead, without Xcode (sign_with_certificate)"""
    if args.p12 or args.profile:
        if not (args.p12 and args.profile): raise Fail('--p12 and --profile go together')
        return sign_with_certificate(Build(args.build), args.p12, args.profile, args.password_stdin)
    if WIN: raise Fail('on Windows: nxs2.py sign <build> --p12 <your certificate .p12> --profile <your .mobileprovision> (docs/WINDOWS.md)')
    if not args.team_id: raise Fail('--team-id <your Apple developer team ID> (Xcode signing), or --p12 and --profile')
    b = Build(args.build); team = args.team_id
    x = b.out / 'xcode'; d = b.out / 'build-ios'; ar = d / 'app.xcarchive'; ex = d / 'export'
    if not (x / 'DmIOS.xcodeproj').is_dir(): raise Fail(f'no Xcode project at {x}: run nxs2.py build ... --xcode --team-id {team} first')
    lst = subprocess.run(['xcodebuild', '-project', x / 'DmIOS.xcodeproj', '-list', '-json'], capture_output=True, text=True)
    schemes = json.loads(lst.stdout).get('project', {}).get('schemes', []) if lst.returncode == 0 else []
    scheme = next((s for s in schemes if TITLE in s), schemes[0] if schemes else None)
    if not scheme: raise Fail(f'no scheme in {x}: {lst.stderr.strip()[-300:]}')
    d.mkdir(parents=True, exist_ok=True)
    if ex.is_dir(): shutil.rmtree(ex)
    (d / 'export.plist').write_text(f'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>method</key><string>{args.method}</string>
<key>teamID</key><string>{team}</string>
<key>signingStyle</key><string>automatic</string>
<key>destination</key><string>export</string>
<key>compileBitcode</key><false/>
<key>thinning</key><string>&lt;none&gt;</string>
<key>stripSwiftSymbols</key><true/>
</dict></plist>
''')
    say(f'archiving {scheme}')
    run(['xcodebuild', '-project', x / 'DmIOS.xcodeproj', '-scheme', scheme, '-configuration', 'Release', '-destination', 'generic/platform=iOS',
         '-archivePath', ar, '-allowProvisioningUpdates', 'archive'], b.logs / 'xcode_archive.log', cwd=x, heartbeat=20, show=r'error:|\*\* ARCHIVE')
    say('exporting the IPA')
    run(['xcodebuild', '-exportArchive', '-archivePath', ar, '-exportOptionsPlist', d / 'export.plist', '-exportPath', ex, '-allowProvisioningUpdates'],
        b.logs / 'xcode_export.log', cwd=x, heartbeat=20, show=r'error:|EXPORT')
    ipas = sorted(ex.glob('*.ipa'))
    if not ipas: raise Fail(f'the export wrote no IPA; see {b.logs / "xcode_export.log"}')
    b.outputs.mkdir(parents=True, exist_ok=True)
    out = b.outputs / f'{NAME.replace(" ", "")}-signed.ipa'; shutil.copyfile(ipas[0], out)
    say(f'signed IPA: {out}  (install: pymobiledevice3 apps install "{out}"; then docs/IPHONE.md for romfs.img)')


# ---------------------------------------------------------------- the Windows game (windows/: our recompiler and runtime, docs/WINDOWS.md)
SAVES_ROOT = r'%APPDATA%\Pokopia Native'       # the Windows game's saves, one per game version: <root>\<version>\save
WORK_ROOT = r'%LOCALAPPDATA%\Pokopia Native'   # its pipeline cache, temp files and logs: <root>\<version>
RUNTIME_INSTALLER = ('tools/s2_dumps.py', 'tools/lower_corpus.py', 'tools/s2_pkg.py', f'profile/{TITLE}/runtime_keys.tsv')   # the on-demand path's steps


def win_env(t):
    """nxshader's environment: the translator through dotnet, ampere-lower, and SPIRV-Tools from the kit (NXSHADER_TOOLS) before any Vulkan SDK"""
    return {'NXSHADER_TOOLS': str(pathlib.Path(t['spirv-opt.exe']).parent), 'AMPERE_LOWER': str(AMPERE), 'PYTHONDONTWRITEBYTECODE': '1', 'PYTHONUTF8': '1'}


def st_win_analyze(b, t):
    """program.db (nxanalyze): the game's functions, control flow, jump tables and bindings, bound to exactly the take-overs of the runtime this build ships
    (its registry, hle_inventory.tsv), then checked against that registry. Seconds."""
    db = b.win / 'analyze' / 'program.db'
    key = fingerprint(*(b.exefs / m for m in b.modules), digest(t['nxanalyze.exe']), digest(t['hle_inventory.tsv']))
    if b.fresh('win_analyze', key) and db.is_file(): return say('analysis: up to date')
    say('analysing the game\'s code (nxanalyze)')
    run([t['nxanalyze.exe'], b.exefs, '--out', db.parent, '--hle', t['hle_inventory.tsv'], '--no-defaults'], b.logs / 'win_analyze.log', show=r'functions|HLE binding')
    run([t['nxanalyze.exe'], '--check-takeovers', db, t['hle_inventory.tsv']], b.logs / 'win_takeovers.log', show=r'^takeover check')
    b.stamp('win_analyze', key)


def st_win_shaders(b, t, jobs):
    """The game's shaders as Vulkan SPIR-V in windows/shaders, the cache the runtime reads (nxshader: the translator's SPIR-V, the Vulkan edits, reflection,
    spirv-val). Shaders already there are kept, so a rebuild translates only new ones. The run-time shaders are not in the game's files: the runtime
    records each the first time the game makes it (windows/shaders/dumps) and translates it there and then."""
    n = max(1, min(4, jobs)); rep = b.logs / 'win_shaders.txt'
    say(f'translating the shaders for Vulkan (nxshader, {n} worker{"s" if n > 1 else ""})')
    if rep.is_file(): rep.unlink()
    run([PY, '-I', t['nxshader/nxshader.py'], '--cache', b.win / 'shaders', '--installer', HERE, 'build', b.out, '--jobs', n, '--report', rep],
        b.logs / 'win_shaders.log', env=win_env(t), ok=(0, 1), heartbeat=60, show=r'^shaders |^spirv-val')
    m = re.search(r'spirv-val \(vulkan1\.3\): (\d+) pass, (\d+) fail', rep.read_text(encoding='utf-8') if rep.is_file() else '')
    if not m or not int(m.group(1)): raise Fail(f'no shader was translated; see {b.logs / "win_shaders.log"}')
    if int(m.group(2)): say(f'  {m.group(2)} shaders failed validation: their draws are skipped and logged ({rep})')


def st_win_fonts(b):
    """Stand-ins for the console's shared fonts from this PC's own fonts (nothing from the console), as the Mac build makes them: the standard font a copy
    of Arial Unicode or Arial (STD_FONT names another), the button icons drawn by nxnative's make_ext_font.py with Arial Rounded Bold or Arial Bold (LETTER_FONT)."""
    d = b.win / 'fonts'; d.mkdir(parents=True, exist_ok=True)
    fonts = pathlib.Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts'
    std = next((p for p in [pathlib.Path(os.environ['STD_FONT'])] if p.is_file()), None) if os.environ.get('STD_FONT') else \
        next((fonts / n for n in ('ARIALUNI.TTF', 'arial.ttf') if (fonts / n).is_file()), None)
    if not std: raise Fail(f'no standard font (Arial) in {fonts}: set STD_FONT=<a Unicode TTF>')
    if not (d / 'standard.ttf').is_file(): shutil.copyfile(std, d / 'standard.ttf')
    letter = os.environ.get('LETTER_FONT') or next((str(fonts / n) for n in ('ARLRDBD.TTF', 'arialbd.ttf') if (fonts / n).is_file()), None)
    if not (d / 'nintendo_ext.ttf').is_file() and letter:
        run([PY, NX / 'dm/tools/make_ext_font.py', d / 'nintendo_ext.ttf', letter], b.logs / 'make_ext_font.log', env=b.env())


def st_win_lift(b, t, jobs):
    """game.dll: every function of the game's code recompiled for x86-64 (nxlift: LLVM, one object per ~1,000 functions, cached by their IR in
    windows/objcache), linked by lld-link against the runtime's import library. Idle priority, at most 4 workers: about 10 minutes at 4, 20 at 2."""
    dll = b.win / 'game.dll'; db = b.win / 'analyze' / 'program.db'
    key = fingerprint(db, digest(t['nxlift.exe']), digest(t['nxrt.lib']), digest(t['lld-link.exe']))
    if b.fresh('win_lift', key) and dll.is_file(): return say('game.dll: up to date')
    n = max(1, min(4, jobs))
    say(f'recompiling the game\'s code for this PC (nxlift, {n} worker{"s" if n > 1 else ""} at idle priority: about {40 // n} minutes)')
    run([t['nxlift.exe'], b.exefs, '--db', db, '--out', b.win / 'lift', '--cache', b.win / 'objcache', '--jobs', n, '--dll', dll,
         '--nxrt-lib', t['nxrt.lib'], '--lld', t['lld-link.exe']], b.logs / 'win_lift.log', heartbeat=60, show=r'^nxlift:')
    if not dll.is_file(): raise Fail(f'nxlift wrote no {dll}; see {b.logs / "win_lift.log"}')
    b.stamp('win_lift', key)


def copy_if_changed(src, dst):
    dst = pathlib.Path(dst); dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.is_file() and dst.stat().st_size == pathlib.Path(src).stat().st_size and dst.read_bytes() == pathlib.Path(src).read_bytes(): return
    try: shutil.copy2(src, dst)
    except PermissionError: raise Fail(f'{dst} is in use: close the game, then build again')


def win_save_dir(b):
    """The save the Windows game plays, from the build's play.json (the default: %APPDATA%\\Pokopia Native\\<version>\\save)"""
    cfg = b.win / 'play.json'
    raw = json.loads(cfg.read_text(encoding='utf-8')).get('save') if cfg.is_file() else None
    return pathlib.Path(os.path.expandvars(raw or rf'{SAVES_ROOT}\{b.version}\save'))


def st_win_play(b, t):
    """The runtime beside the game (windows/runtime: nxrt.exe, SDL3.dll, play.ps1, and for the run-time shaders nxshader with SPIRV-Tools, ampere-lower,
    the translator and the installer's three lowering steps), windows/play.json, and the launcher out/Play Pokopia <version>.cmd. The save it plays is
    %APPDATA%\\Pokopia Native\\<version>\\save, created empty the first time (nxs2.py import-save brings an existing one)."""
    rt = b.win / 'runtime'
    for n in ('nxrt.exe', 'SDL3.dll', 'play.ps1', 'spirv-opt.exe', 'spirv-val.exe'): copy_if_changed(t[n], rt / n)
    if t.get('gamecontrollerdb.txt') and pathlib.Path(t['gamecontrollerdb.txt']).is_file(): copy_if_changed(t['gamecontrollerdb.txt'], rt / 'gamecontrollerdb.txt')   # nxrt reads it beside itself
    nxs = pathlib.Path(t['nxshader/nxshader.py']).parent
    for f in sorted(nxs.glob('*.py')): copy_if_changed(f, rt / 'nxshader' / f.name)
    copy_if_changed(next(p for p in (nxs / 'dmsh_abi.h', nxs.parent / 'nxrt/gfx/dmsh_abi.h') if p.is_file()), rt / 'nxshader' / 'dmsh_abi.h')
    for n in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):   # the kit's SPIRV-Tools are built with the DLL runtime: its app-local copies
        if (pathlib.Path(t['spirv-opt.exe']).parent / n).is_file(): copy_if_changed(pathlib.Path(t['spirv-opt.exe']).parent / n, rt / n)
    for rel in RUNTIME_INSTALLER: copy_if_changed(HERE / rel, rt / 'installer' / rel)
    copy_if_changed(AMPERE, rt / 'installer/ampere-lower/windows/ampere-lower.exe')
    for f in sorted((HERE / 'host/windows/dmsh_xlate_bin').iterdir()): copy_if_changed(f, rt / 'installer/host/windows/dmsh_xlate_bin' / f.name)
    seed = pathlib.Path(os.environ['NXW_PIPELINE_SEED']) if os.environ.get('NXW_PIPELINE_SEED') else PROFILE_SRC / f'pipelines.{b.version}.tsv'
    if seed.is_file(): copy_if_changed(seed, b.win / 'pipelines-seed.tsv')
    cfg = {'pokopia_native': TOOL_VERSION, 'game_version': b.version, 'exe': r'runtime\nxrt.exe', 'exefs': r'..\extract\ExeFS', 'dll': 'game.dll',
           'romfs': r'..\extract\RomFS.img', 'save': rf'{SAVES_ROOT}\{b.version}\save', 'work': rf'{WORK_ROOT}\{b.version}', 'shaders': 'shaders',
           'shader_build': '..', 'fonts': 'fonts', 'pipeline_seed': 'pipelines-seed.tsv' if seed.is_file() else '', 'python': PY,
           'shader_tool': r'runtime\nxshader\nxshader.py', 'installer': r'runtime\installer', 'spirv_tools': 'runtime'}
    old = b.win / 'play.json'
    if old.is_file():   # a save or work folder the player chose stays
        prev = json.loads(old.read_text(encoding='utf-8')); cfg.update({k: prev[k] for k in ('save', 'work') if prev.get(k)})
    old.write_text(json.dumps(cfg, indent=1) + '\n', encoding='utf-8')
    b.outputs.mkdir(parents=True, exist_ok=True)
    play = r'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\windows\runtime\play.ps1" -Config "%~dp0..\windows\play.json"'
    end = 'set "RC=%ERRORLEVEL%"\nif not "%RC%"=="0" pause\nexit /b %RC%\n'   # a failed start keeps its window open, so its message can be read
    cmd = b.outputs / f'Play Pokopia {b.version}.cmd'
    cmd.write_text(f'''@echo off
rem Pokemon Pokopia {b.version} on Windows, built from your own dump by pokopia-native {TOOL_VERSION} (nxs2.py build --windows). Double-click to play.
rem The save is in {cfg["save"]} (windows\\play.json names it; nxs2.py import-save brings one from Eden, the phone or a Mac).
rem A window asks how to play first: alone, Link Play on this network, or hosting / visiting over the internet ("Don't ask again" in it;
rem Pokopia Settings {b.version}.cmd or Shift held while starting shows it again). play.ps1's switches can follow: -Fullscreen, -Window 1600x900, -Silent,
rem -PadCheck; -Save and -Work with other folders for a second player on this PC; -LinkPlay, -Join, -Internet, -Room and -Relay instead of the window (docs/WINDOWS.md).
{play} %*
''' + end, encoding='ascii')
    (b.outputs / f'Pokopia Settings {b.version}.cmd').write_text(f'''@echo off
rem The window before Pokemon Pokopia {b.version} starts (Link Play, Controls, Display), even after "Don't ask again" was ticked. Play in it starts the game.
{play} -Settings %*
''' + end, encoding='ascii')
    exe = t.get('pokopia-launch.exe')   # the same two launchers as programs (no console window, an icon; for a taskbar pin or Steam's "Add a Non-Steam Game")
    if exe and pathlib.Path(exe).is_file():
        copy_if_changed(exe, b.outputs / f'Play Pokopia {b.version}.exe'); copy_if_changed(exe, b.outputs / f'Pokopia Settings {b.version}.exe')
        say(f'Windows game: {b.outputs / f"Play Pokopia {b.version}.exe"}  (or the .cmd beside it; the settings window: Pokopia Settings {b.version}.exe; save: {win_save_dir(b)})')
    else:
        say(f'Windows game: {cmd}  (the settings window: Pokopia Settings {b.version}.cmd beside it; save: {win_save_dir(b)})')
    return cmd


def st_windows(b, jobs):
    """The Windows game from the extracted dump and the shader package: analysis, shaders, fonts, the recompiled game.dll, the runtime and the launcher."""
    t = win_tools()
    if t['missing']: raise Fail(f'the Windows game tools are incomplete in {t.get("dir")}: missing {", ".join(t["missing"])} (nxs2.py doctor)')
    say(f'Windows tools: {t["dir"]}')
    st_win_analyze(b, t); st_win_shaders(b, t, jobs); st_win_fonts(b); st_win_lift(b, t, jobs)
    return st_win_play(b, t)


def eden_saves():
    """Eden's saves of this game, newest first (read, never written): <Eden data>\\nand\\user\\save\\0000000000000000\\<user>\\<title id>, or the title-0
    folder Eden files it under when it cannot read the title ID from an extracted dump. EDEN_DIR names the data folder (default %APPDATA%\\eden)."""
    root = pathlib.Path(os.environ.get('EDEN_DIR') or pathlib.Path(os.environ.get('APPDATA', '')) / 'eden') / 'nand/user/save/0000000000000000'
    c = [d for u in sorted(root.glob('*')) if u.is_dir() for d in (u / TITLE, u / '0000000000000000')
         if (d / 'G00' / 'CMNDAT.BIN').is_file() and (d / 'S00' / 'SYDAT.BIN').is_file()]
    return sorted(c, key=lambda d: max(f.stat().st_mtime for f in d.rglob('*') if f.is_file()), reverse=True)


def import_save(args):
    """A save from elsewhere becomes the one the Windows game plays: a folder holding the game's B00, G00 and S00 (Eden's save folder, the phone's
    Documents/save as tools/phone_pull.py brings it, a Mac's Application Support/nxnative/<title>/save) or one holding such a save/ folder; --eden finds
    Eden's own. The source is only read. What the save folder held before (with the game's commit leftovers) is kept in <save>.before-<time>."""
    b = Build(args.build); dst = win_save_dir(b)
    if args.eden:
        found = eden_saves()
        if not found: raise Fail('no Pokopia save in Eden\'s folder (%APPDATA%\\eden\\nand\\user\\save); give the folder instead')
        src = found[0]; say(f'Eden\'s save: {src}' + (f' (newest of {len(found)})' if len(found) > 1 else ''))
    elif args.folder:
        src = pathlib.Path(args.folder).expanduser().resolve()
        if not (src / 'G00').is_dir() and (src / 'save' / 'G00').is_dir(): src = src / 'save'
    else: raise Fail('give the save folder (it holds B00, G00 and S00), or --eden')
    if not ((src / 'G00').is_dir() and (src / 'S00').is_dir()): raise Fail(f'{src} holds no Pokopia save (no G00 and S00 folders)')
    if src == dst or dst in src.parents: raise Fail(f'{src} is the save the game plays already')
    if WIN and 'nxrt.exe' in subprocess.run(['tasklist', '/FI', 'IMAGENAME eq nxrt.exe'], capture_output=True, text=True).stdout and not args.force:
        raise Fail('the game is running: close it first (or --force)')
    pieces = [p for p in (dst, *(dst.with_name(dst.name + s) for s in ('.work', '.new', '.old', '.commit'))) if p.exists()]
    if pieces:
        keep = dst.with_name(f'{dst.name}.before-{time.strftime("%Y%m%d-%H%M%S")}'); keep.mkdir(parents=True)
        for p in pieces: shutil.move(str(p), str(keep / p.name))
        say(f'the save that was there: {keep}')
    dst.mkdir(parents=True)
    for d in ('B00', 'G00', 'S00'):
        if (src / d).is_dir(): shutil.copytree(src / d, dst / d)
    n = sum(1 for f in dst.rglob('*') if f.is_file())
    say(f'imported {src} -> {dst} ({n} files). Each player needs a save of their own for Link Play: the game refuses a second copy of the host\'s save.')


def run_windows(args):
    b = Build(args.build); cmd = b.outputs / f'Play Pokopia {b.version}.cmd'
    if not cmd.is_file(): raise Fail(f'{b.out} has no Windows game yet: nxs2.py build <dump> --windows')
    say(f'starting {cmd.name}')
    sys.exit(subprocess.call(['cmd', '/c', str(cmd)] + list(args.env)))


# ---------------------------------------------------------------- report
def coverage(b):
    """(file shaders in the map, runtime shaders known for this build's version, runtime shaders present)"""
    rows = [l.split('\t') for l in (PROFILE_SRC / 'runtime_keys.tsv').read_text().splitlines() if l.strip() and not l.startswith('#')]
    every = {f[0] for f in rows}
    known = {f[0] for f in rows if len(f) < 3 or b.version in f[2].split(',')}   # a third column names the only versions that make the shader
    keys = set()
    smap = b.profile / 'shader_map.tsv'
    if smap.is_file():
        for l in smap.read_text().splitlines():
            if l.startswith('#') or l.startswith('package\t') or not l.strip(): continue
            f = l.split('\t')
            if len(f) > 3: keys.add(f[3])
    return len(keys - every), len(known), len(keys & known)


def report(b):
    files, known, present = coverage(b)
    say(f'shaders: {files} from the game files; {present} of the {known} known run-time shaders')
    if present < known:
        print(f'''
  {known - present} shaders the game builds while it runs (opening, character creator, first area) are not in this build yet;
  a draw that needs one is skipped until it is. They are recorded on your device as you play
  (the apps have DM_NVN_SHDUMP on); then:   nxs2.py refresh "{b.out}"   adds them and rebuilds the outputs.
  (iPhone: pull the app's Documents/shdump first: tools/phone_pull.py, see docs/IPHONE.md)''')


def status(args):
    b = Build(args.build)
    apple = not (WIN and (b.win / 'play.json').is_file() and not b.patched.is_dir())   # a Windows-game-only folder has no Apple stages to report
    for name, p in (('ExeFS', b.exefs / 'main'), ('RomFS image', b.romfs_img), ('rewritten modules', b.patched / 'main.patched.bin'), ('Mac host', b.host),
                    ('fonts', b.font / 'nintendo_ext.ttf'), ('shader corpus', b.ctl / 'hostkeys.tsv'), ('lowered shaders', b.maxwell / 'results.tsv'),
                    ('shader package', b.pkg), ('shader map', b.profile / 'shader_map.tsv')):
        if apple or name not in ('rewritten modules', 'Mac host', 'fonts'): print(f'  {"ok     " if p.exists() else "missing"}  {name}')
    try:
        if apple:
            lib = b.lib(); print(f'  {"ok     " if (lib / "inputs.tsv").is_file() else "missing"}  Mac shader libraries ({lib.name})')
            print(f'  {"ok     " if (b.ios / lib.name / "inputs.tsv").is_file() else "missing"}  iPhone shader libraries')
    except Fail as e: print(f'  ?        shader libraries: {e}')
    if (b.win / 'play.json').is_file():
        for name, p in (('Windows: program.db', b.win / 'analyze/program.db'), ('Windows: game.dll', b.win / 'game.dll'), ('Windows: runtime', b.win / 'runtime/nxrt.exe')):
            print(f'  {"ok     " if p.exists() else "missing"}  {name}')
        vk = b.win / 'shaders' / 'vk'
        print(f'  {"ok     " if vk.is_dir() else "missing"}  Windows: Vulkan shaders ({sum(1 for _ in vk.glob("*.spv")) if vk.is_dir() else 0} modules)')
        dumps = b.win / 'shaders' / 'dumps'
        print(f'  save     {win_save_dir(b)}\n  Windows: {sum(1 for _ in dumps.glob("*.ctl")) if dumps.is_dir() else 0} run-time shaders recorded and translated so far')
    for p in sorted(b.outputs.glob('*')) if b.outputs.is_dir() else []: print(f'  output   {p}')
    if apple and (b.profile / 'shader_map.tsv').is_file(): report(b)


# ---------------------------------------------------------------- commands
def need_setup(ipa=True):
    """Setup's tools live in this copy of the repository: a fresh download or a copy in another folder has none until setup runs there."""
    if not (NX / 'dm/host/Makefile').is_file() or not AMPERE.is_file():
        raise Fail(f'this copy of {HERE.name} is not set up yet (nxnative at {NX}, ampere-lower at {AMPERE}): run nxs2.py setup here once '
                   '(or Setup in the window). A new download or a copy in another folder needs it too; your build folders keep working.')
    if WIN and ipa and not (NX / 'win/build_app.py').is_file():
        raise Fail('the iPhone toolchain is not set up in this copy (setup ran with --no-ios): run nxs2.py setup without --no-ios once')


def build(args):
    if args.windows and not WIN: raise Fail('the Windows game is built on Windows (the "for Windows PCs" download): its recompiler and runtime are Windows programs')
    if WIN and (args.mac_app or args.xcode): raise Fail('the Mac app and the Xcode project need a Mac; on Windows the outputs are the Windows game (--windows) and the iPhone IPA (--ipa)')
    if WIN and not (args.ipa or args.windows): raise Fail('on Windows the outputs are the Windows game (--windows) and the iPhone IPA (--ipa): give one or both')
    need_ios = bool(args.ipa) if WIN else not args.no_ios
    unblock_download()
    bad = doctor(need_ios=need_ios, quiet=True, need_windows=bool(args.windows))
    if bad: doctor(need_ios=need_ios, need_windows=bool(args.windows)); raise Fail('install the missing tools above (nxs2.py doctor), then build again')
    need_setup(ipa=bool(args.ipa))
    if args.out: check_writable(pathlib.Path(args.out).expanduser(), 'the build folder')
    nup = pathlib.Path(args.dump).expanduser().resolve() if args.dump else None
    if nup and nup.is_file() and nup.suffix.lower() == '.nup':   # a NUP container: unpacked into the build folder, then read like a dump folder
        args.out = str(pathlib.Path(args.out).expanduser() if args.out else nup.parent / f'{nup.stem}_native')
        pathlib.Path(args.out).mkdir(parents=True, exist_ok=True)
        args.dump = str(nup_unpack(nup, pathlib.Path(args.out)))
    exefs, romfs, ver, disp = find_dump(args)
    if args.out: out = pathlib.Path(args.out).expanduser()
    elif args.dump: d = pathlib.Path(args.dump).expanduser().resolve(); out = d.parent / f'{d.name}_native'   # beside the dump, never inside it
    else: raise Fail('--exefs/--romfs need --out <build folder>')
    b = Build(out); jobs = JOBS
    b.set_version(disp)
    say(f'pokopia-native {TOOL_VERSION}: {NAME} {ver}: dump {exefs.parent}, build folder {b.out}')
    say(f'load: {jobs} worker(s) of {os.cpu_count()} CPU threads, low priority' + ('' if args.full_speed or args.jobs else ' (gentle default; --full-speed uses them all)'))
    free = shutil.disk_usage(b.out).free / (1 << 30)
    if free < 15 and not b.romfs_img.is_file(): say(f'WARNING: {free:.0f} GB free where the build folder is; one game version needs about 12 GB')
    st_profile(b)
    st_extract(b, exefs, romfs)
    apple = not WIN or args.ipa   # the Apple stages: the code rewritten for Apple's kernel, the Metal shader libraries
    if apple: st_rewrite(b)
    if not WIN: st_host(b, jobs); st_font(b)   # the Mac host and its fonts: on Windows the iPhone app's fonts come from win/toolchain.py
    st_corpus(b, romfs, args.dumps or default_dump_dirs(b))   # the dumps refresh would take: a new version's rebuilt corpus keeps the run-time shaders
    st_lower(b, jobs)
    st_package(b)
    if apple: st_chain(b, need_ios)
    if args.windows: st_windows(b, jobs)
    if args.mac_app: st_mac_app(b)
    if args.ipa: st_ipa_windows(b) if WIN else st_ipa(b)
    if args.xcode:
        if not args.team_id: raise Fail('--xcode needs --team-id <your Apple developer team ID>')
        st_xcode(b, args.team_id)
    if not (args.mac_app or args.ipa or args.xcode or args.windows): say('no output asked for (--mac-app, --ipa, --xcode); the build folder is complete for nxs2.py run')
    if apple: report(b)
    if args.windows:
        play = b.outputs / f'Play Pokopia {b.version}.exe'
        say(f'the Windows game: double-click "{play if play.is_file() else b.outputs / f"Play Pokopia {b.version}.cmd"}" (or nxs2.py run "{b.out}")')
    say('done')


def default_dump_dirs(b):
    dirs = [b.rundir / 'shdump', pathlib.Path.home() / 'Library/Application Support/nxnative' / TITLE / 'shdump',
            b.win / 'shaders' / 'dumps']   # the Windows game records the run-time shaders in the same form (and translates them for itself on the spot)
    if b.phone.is_dir(): dirs += sorted(p for p in b.phone.glob('*/shdump') if p.is_dir())
    return [d for d in dirs if d.is_dir()]


def refresh(args):
    b = Build(args.build)
    if not (b.ctl / 'hostkeys.tsv').is_file(): raise Fail(f'{b.out} holds no finished build')
    need_setup()
    dirs = [pathlib.Path(d).expanduser() for d in args.dumps] or default_dump_dirs(b)
    if not dirs: raise Fail('no shader dumps found: give the folder(s) (DM_NVN_SHDUMP output: <key>.ctl + <key>.code files)')
    before = coverage(b)
    add_dumps(b, dirs)
    st_lower(b, JOBS); st_package(b)
    st_chain(b, WIN or (b.ios / b.lib().name / 'inputs.tsv').is_file())   # iPhone libraries again when the build had them
    if not WIN and (b.outputs / f'{NAME}.app').is_dir(): st_mac_app(b)
    if b.outputs.is_dir() and any(b.outputs.glob('*-unsigned.ipa')): st_ipa_windows(b) if WIN else st_ipa(b)
    if not WIN and (b.out / 'xcode' / 'DmIOS.xcodeproj').is_dir():
        team = args.team_id
        pbx = b.out / 'xcode' / 'DmIOS.xcodeproj' / 'project.pbxproj'   # project.yml keeps only ${DM_TEAM_ID}; the generated project has the team itself
        if not team and pbx.is_file():
            m = re.search(r'DEVELOPMENT_TEAM\s*=\s*"?([A-Z0-9]{10})', pbx.read_text()); team = m.group(1) if m else None
        if team: st_xcode(b, team); say(f'the Xcode project is refreshed: nxs2.py sign "{b.out}" --team-id {team} makes the new signed IPA')
        else: say('the Xcode project is not refreshed (give --team-id)')
    after = coverage(b)
    say(f'run-time shaders: {before[2]} -> {after[2]} of {after[1]} known' + (f', plus {after[0] - before[0]} not in the known list' if after[0] != before[0] else ''))
    report(b); say('done')


def run_mac(args):
    """The game in a window on this Mac, straight from the build folder (the host, not the app): saves in <build>/run/save, dumps in <build>/run/shdump."""
    if WIN: raise Fail('playing from the build folder needs a Mac (the host runs the game\'s arm64 code natively); on Windows the output is the iPhone IPA')
    b = Build(args.build)
    for p in (b.host, b.romfs_img, b.patched / 'main.patched.bin'):
        if not p.exists(): raise Fail(f'{p} is missing: run nxs2.py build first')
    rd = b.rundir
    for d in ('save', 'temp', 'shdump'): (rd / d).mkdir(parents=True, exist_ok=True)   # the host writes dumps only into a folder that exists
    env = dict(os.environ, DM_ROOT=str(b.out), DM_ROMFS=str(b.romfs_img), DM_SAVE=str(rd / 'save'), DM_TEMP=str(rd / 'temp'), DM_MSL_DIR=str(b.cache),
               DM_FONT_DIR=str(b.font), DM_NVN_SHMAP=str(b.profile / 'shader_map.tsv'), DM_LANG=args.lang, DM_WINDOW='1', DM_AUDIO='1', DM_NVN_SHDUMP='shdump')
    for kv in args.env: k, _, v = kv.partition('='); env[k] = v
    say(f'starting {NAME} (window; Ctrl-C here or close the window to stop; log {rd / "host.log"})')
    with open(rd / 'host.log', 'w') as log:
        try: rc = subprocess.call([str(b.host), str(b.patched)], env=env, cwd=rd, stdout=log, stderr=subprocess.STDOUT)
        except KeyboardInterrupt: rc = 130
    n = len(list((rd / 'shdump').glob('*.ctl'))) if (rd / 'shdump').is_dir() else 0
    say(f'host exited ({rc}); {n} shaders recorded in {rd / "shdump"}' + (f'  ->  nxs2.py refresh "{b.out}"' if n else ''))


ICON_LOGO = (1205, 140, 1890, 610)   # the title screen's logo badge, in a 1920x1080 picture
ICON_AREA = (1196, 80, 1896, 780)    # the square around it: below the version number, above "Press A"


def pillow():
    try: from PIL import Image; return Image
    except ImportError: raise Fail('the icon needs Pillow: python3 -m pip install pillow')


def is_title(pic):
    """The title screen: its logo badge is lime green where every other screen (logos, language question, the opening) has none (measured: 21 % against 0)."""
    im = pillow().open(pic).convert('RGB').resize((1920, 1080)).crop(ICON_LOGO)
    return sum(1 for r, g, b in pixels(im) if g > 190 and 120 <= r <= 235 and b < 150 and g >= r) / (im.width * im.height) > 0.08


def pixels(im): return im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata()   # Pillow 13 renamed getdata


def make_icon(pic, out):
    """The apps' icon from a picture of the title screen: the logo badge cut out along its white edge, on a soft blur of the screen around it, 1024x1024.
    A square picture (an icon your dumping tool saved, say) is taken as it is."""
    Image = pillow(); from PIL import ImageChops, ImageDraw, ImageEnhance, ImageFilter
    im = Image.open(pic).convert('RGB')
    if abs(im.width / im.height - 1) < 0.05: im.resize((1024, 1024), Image.LANCZOS).save(out); return
    if abs(im.width / im.height - 16 / 9) > 0.05: raise Fail(f'{pic} is {im.width}x{im.height}: give a 16:9 picture of the title screen, or a square icon')
    if not is_title(pic): raise Fail(f'{pic} does not show the title screen (no logo where the title screen has it)')
    im = im.resize((1920, 1080), Image.LANCZOS)
    crop = im.crop(ICON_LOGO); r, g, b = crop.split()
    edge = ImageChops.darker(ImageChops.darker(r, g), b).point(lambda v: 255 if v > 215 else 0)   # the badge's white outline
    for c in ((0, 0), (crop.width - 1, 0), (0, crop.height - 1), (crop.width - 1, crop.height - 1)):   # everything reachable from a corner without crossing it is outside
        if edge.getpixel(c) == 0: ImageDraw.floodfill(edge, c, 128, thresh=0)
    mask = edge.point(lambda v: 0 if v == 128 else 255).filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(1.2))
    if not 0.35 < sum(v > 128 for v in pixels(mask)) / (crop.width * crop.height) < 0.75: raise Fail(f'no logo found where the title screen has it in {pic}')
    bg = im.crop(ICON_AREA).filter(ImageFilter.GaussianBlur(50)).resize((1024, 1024), Image.LANCZOS)
    bg = ImageEnhance.Color(ImageEnhance.Brightness(bg).enhance(1.18)).enhance(1.3)
    w = 880; logo = crop.resize((w, round(crop.height * w / crop.width)), Image.LANCZOS); m = mask.resize(logo.size, Image.LANCZOS)
    x, y = (1024 - logo.width) // 2, (1024 - logo.height) // 2
    bg.paste((40, 60, 40), (x + 8, y + 14), m.filter(ImageFilter.GaussianBlur(14)).point(lambda v: v * 0.45))   # a soft shadow
    bg.paste(logo, (x, y), m); bg.save(out)


def played_save(b, given):
    """A save past the game's opening: a new one asks for the language, then goes straight into the opening and never shows the title screen.
    The given folder, else the build folder's (nxs2.py run), else the Mac app's; it is copied, never used itself."""
    cands = [pathlib.Path(given).expanduser()] if given else [b.rundir / 'save', pathlib.Path.home() / 'Library/Application Support/nxnative' / TITLE / 'save']
    for d in cands:
        if (d / 'G00').is_dir(): return d
    if given: raise Fail(f'{given} holds no game save (no G00 folder)')
    raise Fail('the title screen only shows once a game has been started: play past the opening once (nxs2.py run, or the Mac app), '
               'or give a picture of the title screen (nxs2.py icon <build> <picture>)')


def capture_title(b, png, save):
    """A picture of the title screen from the game itself (Mac): the host on a copy of a played save, until the screen shows the title's logo."""
    for p in (b.host, b.romfs_img, b.patched / 'main.patched.bin'):
        if not p.exists(): raise Fail(f'{p} is missing: run nxs2.py build first')
    rd = b.rundir / 'icon'; shutil.rmtree(rd, ignore_errors=True); rd.mkdir(parents=True)
    shutil.copytree(save, rd / 'save'); (rd / 'temp').mkdir()
    ctl = rd / 'ctl.txt'; ctl.write_text('')
    env = dict(os.environ, DM_ROOT=str(b.out), DM_ROMFS=str(b.romfs_img), DM_SAVE=str(rd / 'save'), DM_TEMP=str(rd / 'temp'), DM_MSL_DIR=str(b.cache),
               DM_FONT_DIR=str(b.font), DM_NVN_SHMAP=str(b.profile / 'shader_map.tsv'), DM_LANG='en-US', DM_AUDIO='0', DM_CTL=str(ctl))
    say('starting the game to take a picture of its title screen (up to 3 minutes)')
    with open(rd / 'host.log', 'w') as log:
        p = subprocess.Popen([str(b.host), str(b.patched)], env=env, cwd=rd, stdout=log, stderr=subprocess.STDOUT)
    try:
        t0, n, seen = time.time(), 0, None
        while time.time() - t0 < 180 and p.poll() is None:
            n += 1; shot = rd / f'shot{n}.png'
            with open(ctl, 'a') as f: f.write(f'shot {shot}\n')
            for _ in range(20):
                if shot.is_file() and shot.stat().st_size: break
                time.sleep(0.25)
            if shot.is_file() and shot.stat().st_size > 150_000 and is_title(shot):   # the boot's frames are blank and compress to a few dozen KB
                if seen: shutil.copy(shot, png); return
                seen = time.time()
            time.sleep(3 if seen else 2)   # once it shows, one more picture a moment later: the logo has finished coming in
        raise Fail(f'the title screen did not show within 3 minutes; see {rd / "host.log"} and the pictures beside it')
    finally:
        p.terminate()
        try: p.wait(10)
        except subprocess.TimeoutExpired: p.kill()


def icon(args):
    b = Build(args.build)
    if args.picture: pic = pathlib.Path(args.picture).expanduser()
    elif WIN: raise Fail('on Windows give a picture: a screenshot of the title screen taken on your Switch 2 (Capture button), or a square icon')
    else: pic = b.rundir / 'title.png'; capture_title(b, pic, played_save(b, args.save))
    make_icon(pic, b.icon)
    say(f'icon: {b.icon}; build again (same command) to put it into the apps')


def main():
    global VERBOSE
    dotnet_on_path()
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument('-v', '--verbose', action='store_true', help='show every line the tools print (default: a heartbeat and the important lines; everything goes to <build>/logs)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    ap.add_argument('--version', action='version', version=f'pokopia-native {TOOL_VERSION}')
    s = sub.add_parser('setup'); s.add_argument('--no-ios', action='store_true', help='without the iPhone toolchain (on Windows: for the Windows game only)'); s.set_defaults(f=setup)
    s = sub.add_parser('doctor'); s.add_argument('--no-ios', action='store_true'); s.add_argument('--no-windows', action='store_true', help="Windows: not the Windows game's tools")
    s.set_defaults(f=lambda a: sys.exit(1 if doctor(not a.no_ios, need_windows=not a.no_windows) else 0))
    s = sub.add_parser('build')
    s.add_argument('dump', nargs='?', help='the dump folder (<game>/updates/<version>/{exefs,romfs} inside, or exefs/ and romfs/ directly), or a .nup file')
    s.add_argument('--exefs'); s.add_argument('--romfs'); s.add_argument('--out', help=f'the build folder (default: <dump>/../{TITLE}_native)')
    s.add_argument('--mac-app', action='store_true'); s.add_argument('--ipa', action='store_true'); s.add_argument('--xcode', action='store_true')
    s.add_argument('--windows', action='store_true', help='Windows: the game recompiled for this PC, with its launcher (out/Play Pokopia <version>.cmd)')
    s.add_argument('--team-id'); s.add_argument('--dumps', nargs='*', default=[]); s.add_argument('--no-ios', action='store_true')
    s.add_argument('-j', '--jobs', type=int, help='worker processes at once (default: all cores on a Mac, a quarter of them, at most 8, on a PC)')
    s.add_argument('--full-speed', action='store_true', help='every CPU thread (fastest; a long all-core load, see docs/WINDOWS.md)')
    s.set_defaults(f=build)
    s = sub.add_parser('refresh'); s.add_argument('build'); s.add_argument('dumps', nargs='*'); s.add_argument('--team-id')
    s.add_argument('-j', '--jobs', type=int); s.add_argument('--full-speed', action='store_true'); s.set_defaults(f=refresh)
    s = sub.add_parser('run'); s.add_argument('build'); s.add_argument('--lang', default='en-US')
    s.add_argument('env', nargs='*', help='Mac: extra DM_* switches, KEY=VALUE; Windows: switches for play.ps1'); s.set_defaults(f=run_windows if WIN else run_mac)
    s = sub.add_parser('import-save', help='Windows: a save from Eden, the phone or a Mac becomes the one the Windows game plays (the old one is kept)')
    s.add_argument('build'); s.add_argument('folder', nargs='?', help='a folder holding B00, G00 and S00 (or a save/ folder with them)')
    s.add_argument('--eden', action='store_true', help="Eden's own save of this game (read only)"); s.add_argument('--force', action='store_true')
    s.set_defaults(f=import_save)
    s = sub.add_parser('sign'); s.add_argument('build'); s.add_argument('--team-id', help='Xcode signing: your Apple developer team ID')
    s.add_argument('--method', default='debugging', help='Xcode export method: debugging (default), ad-hoc, release-testing')
    s.add_argument('--p12', help='without Xcode: your Apple Development certificate and key (.p12)'); s.add_argument('--profile', help='without Xcode: the development provisioning profile (.mobileprovision)')
    s.add_argument('--password-stdin', action='store_true', help='read the .p12 password from the first line of stdin (the window passes it so)'); s.set_defaults(f=sign)
    s = sub.add_parser('status'); s.add_argument('build'); s.set_defaults(f=status)
    s = sub.add_parser('icon', help='the apps\' icon from the title screen: your own game on a Mac, or a picture of it')
    s.add_argument('build'); s.add_argument('picture', nargs='?', help='a 16:9 screenshot of the title screen (your Switch 2\'s Capture button), or a square icon')
    s.add_argument('--save', help='a save past the game\'s opening to boot from (copied; default: the build folder\'s, then the Mac app\'s)'); s.set_defaults(f=icon)
    a = ap.parse_args(); VERBOSE = a.verbose
    if a.cmd == 'build' and not (a.dump or (a.exefs and a.romfs)): ap.error('build needs the dump folder, or --exefs and --romfs')
    global JOBS
    JOBS = max(1, getattr(a, 'jobs', None) or ((os.cpu_count() or 4) if getattr(a, 'full_speed', False) else gentle_jobs()))
    if a.cmd in ('setup', 'build', 'refresh'): lower_priority()
    try: a.f(a)
    except Fail as e: print(f'\n[FAIL] {e}', file=sys.stderr); sys.exit(1)
    except KeyboardInterrupt: print('\ninterrupted', file=sys.stderr); sys.exit(130)


VERBOSE = False
JOBS = 4   # set in main() from -j / --full-speed / gentle_jobs()
if __name__ == '__main__': main()
