"""test_launcher.py <pokopia-launch.exe>: the launcher in a stand-in build folder (out\\, windows\\play.json, windows\\runtime\\play.ps1 that writes down
what it was given): Play passes -Config and the player's switches; the Settings copy adds -Settings; play.ps1's exit code comes back; a missing
build folder says so (POKOPIA_LAUNCH_NOUI=1: on stderr, no message box)."""
import json, os, pathlib, shutil, subprocess, sys, tempfile

exe = pathlib.Path(sys.argv[1])
fails = 0
def check(name, cond, got=''):
    global fails
    print(('ok    ' if cond else 'FAIL  ') + name + ('' if cond else f'   got: {got!r}')); fails += not cond

root = pathlib.Path(tempfile.mkdtemp(prefix='launch-test '))   # a space in the path, as in many players' folders
try:
    (root / 'out').mkdir(); (root / 'windows' / 'runtime').mkdir(parents=True)
    (root / 'windows' / 'play.json').write_text('{}', encoding='utf-8')
    rec = root / 'got.json'
    (root / 'windows' / 'runtime' / 'play.ps1').write_text(
        "$a = @($args)\n"
        f"@{{args = $a; cwd = (Get-Location).Path}} | ConvertTo-Json -Compress | Set-Content -Encoding utf8 '{rec}'\n"
        "Write-Output 'stand-in play.ps1: line one'\nWrite-Output 'the reason it stopped'\n"
        "if ($a -contains '-Fail') { exit 7 } else { exit 0 }\n", encoding='utf-8')
    play, sett = root / 'out' / 'Play Pokopia 2.0.1.exe', root / 'out' / 'Pokopia Settings 2.0.1.exe'
    shutil.copyfile(exe, play); shutil.copyfile(exe, sett)
    env = dict(os.environ, POKOPIA_LAUNCH_NOUI='1')

    r = subprocess.run([str(play), '-Window', '1600x900', '-Silent'], env=env, capture_output=True, text=True, timeout=60)
    got = json.loads(rec.read_text(encoding='utf-8-sig'))
    a = got['args']
    check('Play exits 0', r.returncode == 0, r.returncode)
    check('Play gives play.ps1 -Config windows\\play.json', '-Config' in a and pathlib.Path(a[a.index('-Config') + 1]).resolve() == (root / 'windows' / 'play.json').resolve(), a)
    check('Play passes the player\'s switches', a[-3:] == ['-Window', '1600x900', '-Silent'], a)
    check('Play does not add -Settings', '-Settings' not in a, a)
    check('play.ps1 runs from windows\\', pathlib.Path(got['cwd']).resolve() == (root / 'windows').resolve(), got['cwd'])

    rec.unlink()
    r = subprocess.run([str(sett)], env=env, capture_output=True, text=True, timeout=60)
    a = json.loads(rec.read_text(encoding='utf-8-sig'))['args']
    check('the Settings copy adds -Settings', '-Settings' in a, a)

    r = subprocess.run([str(play), '-Fail'], env=env, capture_output=True, text=True, timeout=60)
    check('play.ps1\'s exit code comes back', r.returncode == 7, r.returncode)
    check('a failure shows play.ps1\'s last lines', 'the reason it stopped' in r.stderr and 'exit code 7' in r.stderr, r.stderr)

    away = pathlib.Path(tempfile.mkdtemp(prefix='launch-away ')); lone = away / 'sub'; lone.mkdir(); shutil.copyfile(exe, lone / 'Play.exe')
    r = subprocess.run([str(lone / 'Play.exe')], env=env, capture_output=True, text=True, timeout=60)
    check('away from a build folder it says so', r.returncode == 2 and "aren't next to this launcher" in r.stderr, (r.returncode, r.stderr))
    shutil.rmtree(away, ignore_errors=True)
finally:
    shutil.rmtree(root, ignore_errors=True)
print('all passed' if not fails else f'{fails} failed')
sys.exit(1 if fails else 0)
