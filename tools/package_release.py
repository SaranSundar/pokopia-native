#!/usr/bin/env python3
"""package_release.py: the release's two downloads, one per computer, into dist/<version>/ (nothing is uploaded; the release is made by hand).

  python tools/package_release.py [--version 1.1.0] [--windows-src DIR] [--windows-build DIR] [--out dist]

  Pokopia-Native-for-Windows.zip   this repository's tools for Windows (the window, nxs2.py, the iPhone toolchain's patches) and windows/bin: the Windows
                                   game's prebuilt tools (nxanalyze, nxlift with LLVM, nxrt, SDL3, lld-link, SPIRV-Tools), their notices and a manifest
  Pokopia-Native-for-Mac.zip       this repository's tools for a Mac (the window app, nxs2.py, ampere-lower's source, the Mac translator)
  SHA256SUMS.txt, CONTENTS.txt     checksums and the file list of both

The asset names carry no version, so the README's links (releases/latest/download/<name>) keep working from one release to the next; the version is in
the zips' top folder (pokopia-native-<version>-windows/), their comment and windows/bin/MANIFEST.txt. Reproducible: the files git tracks or would track
(git ls-files -co --exclude-standard; nothing ignored, so no nxnative/ checkout, build folder or dump), sorted, every entry dated by the last commit.
Nothing derived from the game goes in: the packager refuses any file under a path it knows to be game-derived, and any file larger than the kit's own.
The Windows tools come from windows-native's release build (scripts\\build.cmd: the static C runtime); the packager checks they import no C runtime DLL.
"""
import argparse, hashlib, io, os, pathlib, re, struct, subprocess, sys, time, zipfile

HERE = pathlib.Path(__file__).resolve().parents[1]
WIN_ONLY = ('windows/', 'host/windows/', 'ampere-lower/windows/', 'Pokopia Native Builder.cmd')
MAC_ONLY = ('Pokopia Native Builder.app/', 'host/dmsh_xlate_bin/')
NEVER = ('.github/workflows/', '.github/ISSUE_TEMPLATE/')   # the CI files and issue forms are the repository's, not the players'
GAME_DERIVED = re.compile(r'(^|/)(_local|extract|shader/corpus|shader/maxwell|shdump|dumps|objcache)(/|$)|\.(nso|img|nca|nsp|xci|bin\.patched)$|(^|/)(main|sdk|subsdk0|rtld)$', re.I)
VC_RUNTIME = ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')   # the SPIRV-Tools of the Vulkan SDK are built with the DLL runtime: app-local copies
LLVM_EXCEPTION = '''---- LLVM Exceptions to the Apache 2.0 License ----

   As an exception, if, as a result of your compiling your source code, portions
   of this Software are embedded into an Object form of such source code, you
   may redistribute such embedded portions in such Object form without complying
   with the conditions of Sections 4(a), 4(b) and 4(d) of the License.

   In addition, if you combine or link compiled forms of this Software with
   software that is licensed under the GPLv2 ("Combined Software") and if a
   court of competent jurisdiction determines that the patent provision (Section
   3), the indemnity provision (Section 9) or other Section of the License
   conflicts with the conditions of the GPLv2, you may retroactively and
   prospectively choose to deem waived or otherwise exclude such Section(s) of
   the License, but only in their entirety and only with respect to the Combined
   Software.
'''


def git(*a):
    return subprocess.run(['git', '-C', str(HERE), *a], capture_output=True, check=True).stdout


def tool_version():
    return re.search(r"^TOOL_VERSION = '([^']+)'", (HERE / 'nxs2.py').read_text(encoding='utf-8'), re.M).group(1)


def pe_imports(path):
    """The DLLs a PE file imports (its import directory), lowercased"""
    b = pathlib.Path(path).read_bytes()
    pe = struct.unpack_from('<I', b, 0x3c)[0]
    nsec, optsz = struct.unpack_from('<H', b, pe + 6)[0], struct.unpack_from('<H', b, pe + 20)[0]
    opt = pe + 24; magic = struct.unpack_from('<H', b, opt)[0]
    rva = struct.unpack_from('<I', b, opt + (120 if magic == 0x20b else 104))[0]
    secs = [struct.unpack_from('<IIII', b, pe + 24 + optsz + 40 * i + 8) for i in range(nsec)]   # VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData
    def off(r): return next(r - va + raw for vs, va, rs, raw in secs if va <= r < va + max(vs, rs))
    names, d = [], off(rva) if rva else None
    while d is not None:
        name_rva = struct.unpack_from('<I', b, d + 12)[0]
        if not name_rva: break
        o = off(name_rva); names.append(b[o:b.index(b'\0', o)].decode().lower()); d += 20
    return names


def find_vc_redist():
    root = pathlib.Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/2022'
    hits = sorted(root.glob('*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT'))
    return hits[-1] if hits else None


def kit_files(src, bld, a):
    """{name in windows/bin: (source path, where it comes from)}"""
    k = {n: (bld / 'bin' / n, 'windows-native build') for n in ('nxanalyze.exe', 'nxlift.exe', 'nxrt.exe', 'SDL3.dll')}
    k['nxrt.lib'] = (bld / 'lib' / 'nxrt.lib', 'windows-native build (the import library the game DLL links against)')
    k['hle_inventory.tsv'] = (src / 'src/nxrt/gen/out/hle_inventory.tsv', 'windows-native source (the runtime\'s registry: nxanalyze --hle)')
    k['play.ps1'] = (src / 'scripts/play.ps1', 'windows-native source (the launcher)')
    for f in sorted((src / 'src/nxshader').glob('*.py')): k[f'nxshader/{f.name}'] = (f, 'windows-native source (the shader tool)')
    k['nxshader/dmsh_abi.h'] = (src / 'src/nxrt/gfx/dmsh_abi.h', 'windows-native source (the shader ABI nxshader reflects against)')
    k['lld-link.exe'] = (pathlib.Path(a.lld), 'LLVM for Windows')
    for n in ('spirv-opt.exe', 'spirv-val.exe'): k[n] = (pathlib.Path(a.spirv_tools) / n, 'Vulkan SDK (SPIRV-Tools)')
    gc = src / 'src/nxrt/front/third_party/SDL_GameControllerDB/gamecontrollerdb.txt'   # SDL_GameControllerDB's mappings, read by nxrt beside itself (a source from before them has none)
    if gc.is_file(): k['gamecontrollerdb.txt'] = (gc, 'SDL_GameControllerDB (windows-native source, src/nxrt/front/third_party/SDL_GameControllerDB/README.md names its commit)')
    else: print('note: no gamecontrollerdb.txt in the windows-native source: the kit goes without the extra controller mappings')
    if a.vc_redist:
        for n in VC_RUNTIME: k[n] = (pathlib.Path(a.vc_redist) / n, 'Visual Studio 2022 redistributable (for spirv-opt and spirv-val)')
    return k


def notices(src, a):
    """THIRD-PARTY-NOTICES.txt of windows/bin: every component in the Windows tools, its licence text from the source it came with"""
    rd = lambda p: pathlib.Path(p).read_text(encoding='utf-8', errors='replace').strip()
    apache = rd(src / 'src/nxrt/vk/third_party/astcenc/LICENSE.txt')
    sdl = pathlib.Path(a.sdl3) / 'LICENSE.txt'
    parts = [
        ('pokopia-native for Windows (nxanalyze, nxlift, nxrt, play.ps1, nxshader)', 'MIT', 'this project', rd(HERE / 'LICENSE')),
        ('nxnative (the runtime\'s Horizon services, NVN recording layer and shader pipeline, ported into nxrt and nxshader)', 'MIT',
         'https://github.com/seedds/nxnative', rd(src / 'src/nxrt/third_party/nxnative-LICENSE')),
        ('nvn.h (the NVN types, as carried by nxnative), in nxrt', 'MIT', 'h1k421', '\n'.join(l[3:] if l.startswith(' * ') else l.lstrip(' *') for l in
         rd(src / 'src/nxrt/gfx/nvn.h').split('*/', 2)[1].splitlines() if l.strip() not in ('/*', '*/', '*'))),
        ('deko3d (the texture layout algorithm nxnative\'s nvn_tex.c ports), in nxrt', 'zlib', 'Copyright (C) 2018-2020 fincs', 'zlib licence: see https://github.com/devkitPro/deko3d'),
        ('LLVM 23.1.4 (linked into nxlift.exe; lld-link.exe is its linker)', 'Apache-2.0 WITH LLVM-exception', 'https://llvm.org',
         'The LLVM Project is under the Apache License v2.0 with LLVM Exceptions:\n\n' + apache + '\n\n' + LLVM_EXCEPTION),
        ('SDL 3.4.18 (SDL3.dll: window, input, sound)', 'zlib', 'https://libsdl.org', rd(sdl) if sdl.is_file() else 'zlib licence, Copyright (C) 1997-2026 Sam Lantinga'),
        *([('SDL_GameControllerDB (gamecontrollerdb.txt: controller mappings, read by nxrt)', 'zlib', 'https://github.com/mdqinc/SDL_GameControllerDB',
            rd(src / 'src/nxrt/front/third_party/SDL_GameControllerDB/LICENSE'))] if (src / 'src/nxrt/front/third_party/SDL_GameControllerDB/LICENSE').is_file() else []),
        ('astc-encoder 5.7.0 (ASTC to BC7 transcoding), in nxrt', 'Apache-2.0', 'https://github.com/ARM-software/astc-encoder', apache),
        ('bc7enc_rdo (BC7 encoding), in nxrt', 'MIT or public domain (Unlicense), at your choice', 'https://github.com/richgel999/bc7enc_rdo',
         rd(src / 'src/nxrt/vk/third_party/bc7enc/LICENSE')),
        ('Vulkan headers (compiled into nxrt)', 'Apache-2.0 OR MIT', 'https://github.com/KhronosGroup/Vulkan-Headers', 'Copyright 2015-2026 The Khronos Group Inc.; '
         'SPDX-License-Identifier: Apache-2.0 OR MIT (the Apache-2.0 text is above)'),
        ('SPIRV-Tools v2026.4 (spirv-opt.exe, spirv-val.exe, from the Vulkan SDK 1.4.363)', 'Apache-2.0', 'https://github.com/KhronosGroup/SPIRV-Tools',
         'Copyright The Khronos Group Inc. and the SPIRV-Tools contributors; Apache-2.0 (the text is above)'),
        ('Microsoft Visual C++ runtime (msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll, for spirv-opt and spirv-val; the static C runtime in our own tools)',
         'Microsoft Visual Studio 2022 licence, Distributable Code', 'Microsoft', 'Redistributed unmodified as permitted by the Visual Studio 2022 licence terms '
         '(Distributable Code, REDIST.TXT). The C++ standard library code in our tools is the MSVC STL: Apache-2.0 WITH LLVM-exception.'),
        ('Ryujinx shader translator (dmsh_xlate: Ryujinx.Graphics.Shader, Ryujinx.Common, Spv.Generator), run by nxshader; in host/windows/dmsh_xlate_bin', 'MIT',
         'Ryujinx Team and Contributors', 'See host/NOTICE.md in this download.'),
        ('ampere-lower (the Switch 2 shader lowering, run by nxshader; source and licence in ampere-lower/)', 'GPL-3.0-or-later', 'Eden-NS2',
         'See ampere-lower/LICENSE and ampere-lower/NOTICE.md in this download.'),
    ]
    out = ['Third-party notices for windows/bin (the Windows game\'s tools in pokopia-native\'s "for Windows PCs" download)', '',
           'The tools are built from this project\'s own MIT sources and the components below. None of them contains anything of the game: the game\'s code',
           'is recompiled, and its shaders translated, on your PC from your own dump, into your build folder.', '']
    for i, (what, lic, who, text) in enumerate(parts, 1): out.append(f'{i:2d}. {what}\n    {lic}; {who}')
    for what, lic, who, text in parts: out += ['', '=' * 100, f'{what}', f'{lic}; {who}', '-' * 100, text]
    return '\n'.join(out).replace('\r\n', '\n') + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument('--version', default=None, help='default: nxs2.py\'s TOOL_VERSION')
    ap.add_argument('--windows-src', default=None, help='the windows-native source (default: windows/ here, else ../windows-native)')
    ap.add_argument('--windows-build', default=None, help='its release build folder (default: <windows-src>/build/Release)')
    ap.add_argument('--lld', default=None, help='lld-link.exe (default: LLVM for Windows)')
    ap.add_argument('--spirv-tools', default=None, help='the folder with spirv-opt.exe and spirv-val.exe (default: the newest Vulkan SDK\'s Bin)')
    ap.add_argument('--vc-redist', default=None, help='the Visual C++ runtime DLLs for them (default: Visual Studio 2022\'s Redist folder; "none" to leave them out)')
    ap.add_argument('--sdl3', default=None, help='the SDL3 development package (its LICENSE.txt; default: <windows-src>/../toolchains/SDL3-3.4.18)')
    ap.add_argument('--out', default=str(HERE / 'dist'))
    ap.add_argument('--rules', help='a private file (never published) of "never<TAB>regex" lines (names and values no packaged file may contain) and '
                    '"allow<TAB>path<TAB>regex" lines (a known, accepted match: an upstream author\'s address, a build machine\'s generic account)')
    ap.add_argument('--strict', action='store_true', help='fail when a packaged file names a user folder or matches --rules (release day)')
    a = ap.parse_args()
    personal = [re.compile(rb'[A-Za-z]:\\Users\\[A-Za-z0-9._ -]{1,40}\\|/Users/[A-Za-z0-9._-]{1,40}/', re.I)]
    allowed = []
    if a.rules:
        for line in pathlib.Path(a.rules).read_text(encoding='utf-8').splitlines():
            kind, _, rest = line.partition('\t')
            if kind == 'never' and rest: personal.append(re.compile(rest.encode(), re.I))
            elif kind == 'allow' and '\t' in rest: path, _, rx = rest.partition('\t'); allowed.append((path, re.compile(rx, re.I)))
    found = set()
    version = a.version or tool_version()
    if version != tool_version(): print(f'note: --version {version}, nxs2.py says {tool_version()}')
    src = pathlib.Path(a.windows_src) if a.windows_src else next(p for p in (HERE / 'windows', HERE.parent / 'windows-native') if (p / 'src/nxrt').is_dir())
    bld = pathlib.Path(a.windows_build) if a.windows_build else src / 'build' / 'Release'
    if not a.lld:
        a.lld = next((str(p) for p in (pathlib.Path(os.environ.get('ProgramFiles', r'C:\Program Files')) / 'LLVM/bin/lld-link.exe',) if p.is_file()), '')
    if not a.spirv_tools:
        sdks = sorted(pathlib.Path('C:/VulkanSDK').glob('*/Bin/spirv-opt.exe'))
        a.spirv_tools = str(sdks[-1].parent) if sdks else ''
    if a.vc_redist is None: a.vc_redist = str(find_vc_redist() or '')
    if a.vc_redist == 'none': a.vc_redist = ''
    if not a.sdl3: a.sdl3 = str(src.parent / 'toolchains' / 'SDL3-3.4.18')

    kit = kit_files(src, bld, a)
    missing = [f'{n} ({p})' for n, (p, _) in kit.items() if not p.is_file()]
    if missing: sys.exit('missing for windows/bin:\n  ' + '\n  '.join(missing))
    for n in ('nxanalyze.exe', 'nxlift.exe', 'nxrt.exe', 'lld-link.exe'):   # our tools carry the C runtime (CMakeLists.txt: /MT): nothing to install on the player's PC
        bad = [d for d in pe_imports(kit[n][0]) if d.startswith(('msvcp', 'vcruntime', 'api-ms-win-crt', 'ucrtbase'))]
        if bad: sys.exit(f'{kit[n][0]} imports {", ".join(bad)}: build windows-native with the static C runtime (its CMakeLists.txt default) and package again')
    if not a.vc_redist:
        print('note: no Visual C++ runtime DLLs beside spirv-opt / spirv-val: they need the Visual C++ Redistributable on the player\'s PC')

    # the repository's files: tracked, or untracked and not ignored; the worktree's content (a release is packaged from a clean, committed tree)
    names = sorted(set(git('ls-files', '-co', '--exclude-standard', '-z').decode('utf-8').split('\0')) - {''})
    names = [n for n in names if (HERE / n).is_file() and not n.startswith(('dist/', 'windows/bin/'))]
    modes = {}
    for line in git('ls-files', '-s', '-z').decode('utf-8').split('\0'):
        if line: m, _, _, path = line.split(None, 3); modes[path] = int(m, 8)
    head = git('rev-parse', 'HEAD').decode().strip()
    dirty = bool(git('status', '--porcelain', '--untracked-files=normal').strip())
    epoch = int(os.environ.get('SOURCE_DATE_EPOCH') or git('log', '-1', '--format=%ct').decode().strip())
    stamp = time.gmtime(max(epoch, 315532800))[:6]   # zip dates start in 1980
    bad = [n for n in names if GAME_DERIVED.search(n)]
    if bad: sys.exit('refusing: these paths look game-derived:\n  ' + '\n  '.join(bad))

    out = pathlib.Path(a.out) / version; out.mkdir(parents=True, exist_ok=True)
    manifest = ['# windows/bin of pokopia-native ' + version + f' (source {head[:12]}{" + uncommitted changes" if dirty else ""}): file, bytes, sha256, origin']
    kit_blobs = {}
    for n, (p, origin) in sorted(kit.items()):
        data = p.read_bytes(); kit_blobs[n] = data
        manifest.append(f'{n}\t{len(data)}\t{hashlib.sha256(data).hexdigest()}\t{origin}')
    kit_blobs['THIRD-PARTY-NOTICES.txt'] = notices(src, a).encode('utf-8')
    kit_blobs['README.txt'] = (f'pokopia-native {version}: the Windows game\'s tools. nxs2.py (and the window) use them from here for "Windows: play on this PC".\n'
                               'Nothing in this folder comes from the game. MANIFEST.txt lists every file with its checksum and origin; THIRD-PARTY-NOTICES.txt\n'
                               'has the licences. Source: the windows/ folder of https://github.com/xFractals/pokopia-native (where published).\n').encode()
    kit_blobs['MANIFEST.txt'] = ('\n'.join(manifest) + '\n').encode()
    biggest = max(len(d) for d in kit_blobs.values())

    sums, contents = [], []
    for plat, asset, skip in (('windows', 'Pokopia-Native-for-Windows.zip', MAC_ONLY + NEVER), ('mac', 'Pokopia-Native-for-Mac.zip', WIN_ONLY + NEVER)):
        top = f'pokopia-native-{version}-{plat}/'
        files = [n for n in names if not n.startswith(skip) and n != skip]
        entries = [(n, (HERE / n).read_bytes(), 0o755 if modes.get(n, 0o100644) & 0o111 else 0o644) for n in files]
        if plat == 'windows': entries += [(f'windows/bin/{n}', d, 0o644) for n, d in kit_blobs.items()]
        for n, d, _ in entries:
            if len(d) > biggest: sys.exit(f'refusing: {n} is larger than any tool ({len(d)} bytes)')
            for rx in personal:   # in text and inside binaries (UTF-8 and UTF-16): a build machine's user folder, the private --rules list
                for m in set(rx.findall(d)) | set(rx.findall(d.decode('utf-16-le', 'ignore').encode('utf-8', 'ignore'))):
                    s = m[:80].decode('utf-8', 'replace') if isinstance(m, bytes) else m
                    if not any(p == n and ok.search(s) for p, ok in allowed): found.add(f'{asset}: {n}: {s}')
        entries.sort(key=lambda e: e[0])
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            z.comment = f'pokopia-native {version} for {"Windows PCs" if plat == "windows" else "Macs"}; source {head}{" (dirty)" if dirty else ""}'.encode()
            dirs = sorted({top + '/'.join(n.split('/')[:i]) + '/' for n, _, _ in entries for i in range(1, n.count('/') + 1)} | {top})
            for d in dirs:
                zi = zipfile.ZipInfo(d, stamp); zi.external_attr = (0o40755 << 16) | 0x10; zi.create_system = 3; z.writestr(zi, b'')
            for n, d, mode in entries:
                zi = zipfile.ZipInfo(top + n, stamp); zi.external_attr = (0o100000 | mode) << 16; zi.create_system = 3
                zi.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(zi, d, compresslevel=9)
        (out / asset).write_bytes(buf.getvalue())
        sums.append(f'{hashlib.sha256(buf.getvalue()).hexdigest()}  {asset}')
        contents += [f'== {asset} ({len(entries)} files, {sum(len(d) for _, d, _ in entries)} bytes unpacked, {len(buf.getvalue())} bytes)'] + \
                    [f'{len(d):>11}  {top}{n}' for n, d, _ in entries] + ['']
        print(f'{out / asset}: {len(entries)} files, {len(buf.getvalue()) / 1048576:.1f} MB')
    (out / 'SHA256SUMS.txt').write_text('\n'.join(sums) + '\n', encoding='utf-8')
    (out / 'CONTENTS.txt').write_text('\n'.join(contents), encoding='utf-8')
    print(f'{out / "SHA256SUMS.txt"}\n' + '\n'.join(sums) + (f'\nnote: packaged from a tree with uncommitted changes (source {head[:12]})' if dirty else ''))
    if found:   # written beside the zips (dist/ is never published); --strict makes it a failure
        (out / 'PUBLISHING-CHECK.txt').write_text('Packaged files that name a user folder or match --rules:\n' + '\n'.join(sorted(found)) + '\n', encoding='utf-8')
        print(f'{"FAIL" if a.strict else "WARNING"}: {len(found)} packaged file(s) name a user folder or match --rules: {out / "PUBLISHING-CHECK.txt"}')
        if a.strict: sys.exit(1)
    elif (out / 'PUBLISHING-CHECK.txt').exists(): (out / 'PUBLISHING-CHECK.txt').unlink()


if __name__ == '__main__':
    main()
