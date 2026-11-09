#!/usr/bin/env python3
"""snapshot_windows.py <windows-native checkout> [--out windows] [--nxnative DIR]: the Windows port's source as a plain copy in windows/, without its
git history: the files its git tracks or would track (git ls-files -co --exclude-standard; never _local/, build folders or anything ignored), with the
publishing audit's fixes applied (windows/docs/M7-PUBLISHING-AUDIT.md). It replaces windows/ except windows/bin and windows/build (the kit and a local build).

Fixes: this PC's absolute paths become placeholders; the location of the game's built-in LDN passphrase is left out (the bytes were never in any
file). After the copy every text file is checked for what must never be published (user folders; with --rules, the private list of names, addresses
and values) and the run fails if any is left. Upstream nxnative's reference files in src/s2tex/tests/ref are checked byte for byte against nxnative
c1df4dd (git's blob ids, without opening them) when --nxnative names a checkout.
"""
import argparse, hashlib, pathlib, re, shutil, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parents[1]
WORK = re.escape(HERE.parent.name)   # this PC's work folder, the one this checkout sits in: its name is not written here
FIXES = [   # (pattern, replacement, why)
    (re.compile(rf'[A-Za-z]:\\Users\\[^\\\s`]+\\Downloads\\{WORK}\\'), r'<work>\\', 'this PC\'s work folder'),
    (re.compile(r'[A-Za-z]:\\Users\\[^\\\s`]+\\Downloads\\update_exefs\\exefs'), r'<2.0.1 ExeFS>', 'this PC\'s dump path'),
    (re.compile(r'`Downloads\\base_exefs`'), r'`<another ExeFS>`', 'this PC\'s dump path'),
    (re.compile(rf'{WORK}([\\/])'), r'<work>\1', 'this PC\'s work folder'),
    (re.compile(r'main\'s read-only data \(ro\+0x[0-9a-f]+\)'), r"main's read-only data (location not published)", 'the LDN passphrase\'s location'),
]
# Built in, only what names nobody: a user folder on Windows or a Mac, this PC's work folder. The names, addresses and values that must not be
# published are not written here (this script is published): --rules names a private file with more "never<TAB>regex" lines.
NEVER = [re.compile(p, re.I) for p in (r'[A-Za-z]:\\Users\\', r'/c/Users/', r'/Users/[A-Za-z]', WORK)]
README_OLD = 'This repository stays local and private until the game boots (milestone M4). Rules: [docs/PROVENANCE.md](docs/PROVENANCE.md).'
README_NEW = ('Published inside [pokopia-native](../README.md) as `windows/`, from release 1.1.0 (a snapshot of the private development repository, without its '
              'history; `docs/M7-REPORT.md`). Rules: [docs/PROVENANCE.md](docs/PROVENANCE.md).')
UPSTREAM_REF = {'src/s2tex/tests/ref/tex/nvn_deswizzle.c': 'dm/gfx/tex/nvn_deswizzle.c', 'src/s2tex/tests/ref/tex/nvn_deswizzle.h': 'dm/gfx/tex/nvn_deswizzle.h',
                'src/s2tex/tests/ref/nvn/nvn_tex.c': 'dm/gfx/nvn/nvn_tex.c', 'src/s2tex/tests/ref/nvn/nvn_tex.h': 'dm/gfx/nvn/nvn_tex.h'}
TEXT = {'.c', '.cpp', '.h', '.hpp', '.py', '.md', '.txt', '.cmd', '.ps1', '.tsv', '.tbl', '.inc', '.S', '.m', '.metal', '.sh', '.diff', '.frag', '.vert',
        '.comp', '.json', '.gitignore', '.cmake', '.dynlist', ''}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('src'); ap.add_argument('--out', default=str(HERE / 'windows')); ap.add_argument('--nxnative', default=str(HERE / 'nxnative'))
    ap.add_argument('--rules', help='a private file (never published) of "never<TAB>regex" lines: names, addresses and values that must not appear')
    a = ap.parse_args()
    if a.rules:
        for line in pathlib.Path(a.rules).read_text(encoding='utf-8').splitlines():
            kind, _, rx = line.partition('\t')
            if kind == 'never' and rx: NEVER.append(re.compile(rx, re.I))
    else: print('note: no --rules file: only the built-in checks (user folders, the work folder) run')
    src, out = pathlib.Path(a.src).resolve(), pathlib.Path(a.out).resolve()
    names = sorted(set(subprocess.run(['git', '-C', str(src), 'ls-files', '-co', '--exclude-standard', '-z'], capture_output=True, check=True)
                       .stdout.decode('utf-8').split('\0')) - {''})
    names = [n for n in names if (src / n).is_file() and not n.startswith(('_local/', 'build/', 'out/'))]
    head = subprocess.run(['git', '-C', str(src), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    if out.is_dir():
        for p in out.iterdir():
            if p.name not in ('bin', 'build'): shutil.rmtree(p) if p.is_dir() else p.unlink()
    fixed, problems, skipped_scan = {}, [], []
    for n in names:
        data = (src / n).read_bytes(); dst = out / n; dst.parent.mkdir(parents=True, exist_ok=True)
        if n in UPSTREAM_REF:   # never opened: copied as bytes, checked by blob id below
            dst.write_bytes(data); skipped_scan.append(n); continue
        if pathlib.Path(n).suffix in TEXT or pathlib.Path(n).name in ('CMakeLists.txt', 'LICENSE', 'LICENSE.txt', 'nxnative-LICENSE'):
            t = data.decode('utf-8')
            for pat, rep, why in FIXES:
                t, k = pat.subn(rep, t)
                if k: fixed.setdefault(n, []).append(f'{why} ({k})')
            if n == 'README.md' and README_OLD in t:
                t = t.replace(README_OLD, README_NEW).replace('# pokopia-native for Windows (private, in development)', '# pokopia-native for Windows')
                fixed.setdefault(n, []).append('the "local and private" lines')
            for pat in NEVER:
                for m in pat.finditer(t): problems.append(f'{n}:{t.count(chr(10), 0, m.start()) + 1}: {m.group(0)}')
            data = t.encode('utf-8')
        else: problems.append(f'{n}: not a known text type (binary files are not published from this tree)')
        dst.write_bytes(data)
    ref_ok = []
    nx = pathlib.Path(a.nxnative)
    for n, up in UPSTREAM_REF.items():
        if n not in names: continue
        mine = subprocess.run(['git', 'hash-object', str(src / n)], capture_output=True, text=True).stdout.strip()
        theirs = subprocess.run(['git', '-C', str(nx), 'rev-parse', f'c1df4dd:{up}'], capture_output=True, text=True).stdout.strip() if (nx / '.git').exists() else ''
        ref_ok.append(f'{n}: {"identical to" if mine and mine == theirs else "NOT CHECKED against" if not theirs else "DIFFERS from"} nxnative c1df4dd:{up}')
        if theirs and mine != theirs: problems.append(f'{n}: differs from upstream nxnative c1df4dd:{up}')
    (out / 'SNAPSHOT.md').write_text(
        f'# This folder is a snapshot\n\nThe Windows port\'s source, copied from its private development repository at commit `{head[:12]}` (plus the changes '
        f'not committed there yet) by `tools/snapshot_windows.py`, without the history. {len(names)} files. Fixes applied for publishing '
        f'(docs/M7-PUBLISHING-AUDIT.md):\n\n' + ''.join(f'- `{n}`: {", ".join(w)}\n' for n, w in sorted(fixed.items())) +
        '\nUpstream nxnative\'s reference files for the s2tex test, copied unchanged and checked by git blob id:\n\n' + ''.join(f'- {r}\n' for r in ref_ok),
        encoding='utf-8')
    print(f'{out}: {len(names)} files from {src} at {head[:12]}; fixed {sum(len(v) for v in fixed.values())} things in {len(fixed)} files')
    for r in ref_ok: print('  ' + r)
    if problems:
        print('PROBLEMS (fix the source or the rules, then run again):\n  ' + '\n  '.join(problems)); sys.exit(1)


if __name__ == '__main__':
    main()
