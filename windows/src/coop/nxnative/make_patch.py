#!/usr/bin/env python3
"""Splices the co-op changes (apply_coop.py) into pokopia-native's host/nxnative-switch2.patch: every file apply_coop.py touches gets its section from
`git diff <upstream commit>` in the checkout, every other section is copied through byte for byte, and the sections stay in git's path order.
    python3 make_patch.py <nxnative checkout> <old patch> <new patch> [upstream commit, default c1df4dd]
The checkout must be the upstream commit with the old patch and apply_coop.py applied. New files are marked intent-to-add in its index (git add -N)."""
import pathlib, re, subprocess, sys

nx, old, new = (pathlib.Path(a).expanduser() for a in sys.argv[1:4])
base = sys.argv[4] if len(sys.argv) > 4 else 'c1df4dd'
NEW_FILES = ['dm/host/coop.h', 'dm/host/coop_int.h'] + [f'dm/host/hle_coop_{m}.c' for m in ('auth', 'disc', 'interact', 'ldn', 'mpnet', 'plat', 'vnet')]
CHANGED = ['dm/games/settings.py', 'dm/games/settings.tsv', 'dm/host/dm.h', 'dm/host/hle_misc.c', 'dm/host/hle_net.c', 'dm/host/hle_offline.c', 'dm/ios/Sources/LauncherRow.swift',
           'dm/ios/Sources/LauncherSettings.swift', 'dm/ios/project.yml', 'dm/mac/launcher.c', 'dm/mac/pack_app.py', 'dm/tools/tests/test_launcher_settings.py',
           'docs/env_switches.tsv', 'docs/frontends.md', 'docs/launcher.schema.json']


def git(*a): return subprocess.run(['git', '-C', str(nx), *a], capture_output=True, check=True).stdout


def sections(data):   # the patch as {path: section bytes}, in order; latin-1 keeps every byte as it is
    text = data.decode('latin-1')
    parts = re.split(r'(?m)^(?=diff --git a/)', text)
    if parts[0].strip(): sys.exit('the patch has text before its first diff')
    out = {}
    for p in parts[1:]:
        m = re.match(r'diff --git a/(\S+) b/\S+\n', p)
        if not m or m.group(1) in out: sys.exit(f'unexpected section: {p[:80]!r}')
        out[m.group(1)] = p
    return out


git('add', '-N', '--', *NEW_FILES)
secs = sections(old.read_bytes())
order = list(secs)   # the old patch's order; a new path goes before the first one that sorts after it
for path in NEW_FILES + CHANGED:
    d = git('diff', '--no-color', '--no-ext-diff', base, '--', path).decode('latin-1')
    if not d.startswith('diff --git a/' + path + ' '): sys.exit(f'{path}: no change against {base}')
    if path not in secs: order.insert(next((i for i, p in enumerate(order) if p.encode() > path.encode()), len(order)), path)
    secs[path] = d
out = ''.join(secs[p] for p in order)
new.write_bytes(out.encode('latin-1'))
print(f'{new}: {len(secs)} files ({len(NEW_FILES)} new, {len(CHANGED)} changed by co-op)')
