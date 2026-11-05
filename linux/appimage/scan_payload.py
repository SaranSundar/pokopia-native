"""The AppImage's contents checked before they are packed, as tools/package_release.py checks a zip: every file (and its UTF-16 reading) against
the user-folder patterns, /home/<name>/ and a rules file's "never" lines minus its "allow" lines; names that look game-derived; videos and debug
symbols. Prints file names and counts only, never what matched (the rules hold private names). Exit 1 on anything found.
An allow line excuses only its own match in its own files (a path regex over the AppDir, or a path of the download as the zip's rules name it):
allow.tsv beside this script has the known third-party ones (upstream authors' addresses, a byte sequence in a .NET DLL, LLVM's build account).
   scan_payload.py <AppDir> <rules.tsv>"""
import pathlib, re, sys

root, rules = pathlib.Path(sys.argv[1]), sys.argv[2]
personal = [re.compile(rb'[A-Za-z]:\\Users\\[A-Za-z0-9._ -]{1,40}\\|/Users/[A-Za-z0-9._-]{1,40}/|/home/[A-Za-z0-9._-]{1,40}/', re.I)]
allowed = []
for src in (rules, pathlib.Path(__file__).with_name('allow.tsv')):
    for line in open(src, encoding='utf-8').read().splitlines():
        kind, _, rest = line.partition('\t')
        if kind == 'never' and rest: personal.append(re.compile(rest.encode(), re.I))
        elif kind == 'allow' and '\t' in rest: path, _, rx = rest.partition('\t'); allowed.append((path, re.compile(rx, re.I)))
def excused(rel, s):
    inst = rel[len('payload/installer/'):] if rel.startswith('payload/installer/') else None
    return any((re.fullmatch(p, rel) or p == inst) and ok.search(s) for p, ok in allowed)
GAME_DERIVED = re.compile(r'(^|/)(_local|extract|shader/corpus|shader/maxwell|shdump|dumps|objcache|exefs|romfs)(/|$)|'
                          r'\.(nso|img|nca|nsp|xci|nup|bin\.patched)$|(^|/)(main|sdk|subsdk0|rtld)$', re.I)
MEDIA = re.compile(r'\.(mp4|m4v|mov|webm|mkv|avi|pdb)$', re.I)
hits, names, media, n = {}, [], [], 0
for f in sorted(root.rglob('*')):
    if f.is_symlink() or not f.is_file(): continue
    rel = f.relative_to(root).as_posix(); n += 1
    if GAME_DERIVED.search(rel): names.append(rel)
    if MEDIA.search(rel): media.append(rel)
    d = f.read_bytes(); u = d.decode('utf-16-le', 'ignore').encode('utf-8', 'ignore')
    for rx in personal:
        for m in set(rx.findall(d)) | set(rx.findall(u)):
            s = m[:80].decode('utf-8', 'replace') if isinstance(m, bytes) else m
            if not excused(rel, s): hits[rel] = hits.get(rel, 0) + 1
print(f'{root}: {n} files')
print(f'game-derived names: {len(names)}' + ''.join(f'\n  {x}' for x in names[:20]))
print(f'video or .pdb files: {len(media)}' + ''.join(f'\n  {x}' for x in media[:20]))
print(f'private-data hits: {sum(hits.values())}' + ''.join(f'\n  {x}: {c}' for x, c in list(hits.items())[:40]))
sys.exit(1 if names or media or hits else 0)
