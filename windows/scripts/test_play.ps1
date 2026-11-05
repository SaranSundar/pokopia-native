# test_play.ps1 -Repo <repository> (ctest scripts.play_defaults, M6 phase B): play.ps1 finds nxrt.exe and its work folder without -Exe or -Work however it is started:
# with -File from another folder, as a script block (no script path: $PSScriptRoot is empty there), and through play.cmd. Stand-in files only, -DryRun: nothing starts.
param([Parameter(Mandatory = $true)][string]$Repo)
$ErrorActionPreference = "Stop"
if (-not (Test-Path (Join-Path $Repo "build\Release\bin\nxrt.exe"))) { Write-Host "SKIP: no build\Release\bin\nxrt.exe in $Repo (play.ps1 looks there)"; exit 77 }
$t = Join-Path ([System.IO.Path]::GetTempPath()) ("nxrt-test-play-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force "$t\ExeFS", "$t\save\B00" | Out-Null
foreach ($f in "ExeFS\main", "ExeFS\sdk", "game.dll", "RomFS.img") { Set-Content -Path "$t\$f" -Value "stand-in" }
$play = Join-Path $Repo "scripts\play.ps1"; $cmd = Join-Path $Repo "scripts\play.cmd"
$a = "-ExeFS `"$t\ExeFS`" -Dll `"$t\game.dll`" -RomFS `"$t\RomFS.img`" -Save `"$t\save`" -DryRun"
$fail = 0
function Check($name, $out) {
    $exe = ($out | Where-Object { $_ -like "exe: *" }) -replace "^exe: ", ""; $work = ($out | Where-Object { $_ -like "work: *" }) -replace "^work: ", ""
    if ($exe -and (Test-Path $exe) -and $exe -like "*nxrt.exe" -and $work -and [System.IO.Path]::IsPathRooted($work)) { Write-Host "ok: $name -> $exe, work $work" }
    else { Write-Host "FAIL: $name -> exe '$exe', work '$work'`n$($out -join "`n")"; $script:fail++ }
}
Push-Location $t
try {
    Check "-File from another folder" (powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$play' $a" 2>&1)
    Check "play.cmd" (cmd /c "`"$cmd`" $a" 2>&1)
} finally { Pop-Location }
Push-Location $Repo
try {   # a script block has no script path: the repository's build folder is found from the current one
    Check "a script block" (powershell -NoProfile -Command "& ([scriptblock]::Create((Get-Content -Raw '$play'))) $a" 2>&1)
} finally { Pop-Location }
# M7: an installer build's play.json (paths relative to its folder, %VARIABLES% expanded, a new save folder with a space); a switch on the command line wins
New-Item -ItemType Directory -Force "$t\b\windows\shaders" | Out-Null
$exe = (Resolve-Path (Join-Path $Repo "build\Release\bin\nxrt.exe")).Path
@{ exe = $exe; exefs = "..\..\ExeFS"; dll = "..\..\game.dll"; romfs = "..\..\RomFS.img"; save = "%NXRT_TEST_PLAY%\new save"; work = "work"; shaders = "shaders";
   shader_build = ".."; shader_tool = "runtime\nxshader\nxshader.py"; python = "C:\nowhere\python.exe"; installer = "runtime\installer"; spirv_tools = "runtime";
   fonts = "fonts"; pipeline_seed = "" } | ConvertTo-Json | Set-Content -Encoding UTF8 "$t\b\windows\play.json"
$env:NXRT_TEST_PLAY = $t
$out = powershell -NoProfile -ExecutionPolicy Bypass -File $play -Config "$t\b\windows\play.json" -Window 1280x720 -DryRun 2>&1
$want = @("exe: $exe", "work: $t\b\windows\work", "--save $t\new save", "--dll $t\game.dll", "NXRT_SHADER_CACHE=$t\b\windows\shaders", "NXRT_SHADER_BUILD=$t\b",
          "--installer `"$t\b\windows\runtime\installer`"")
$miss = $want | Where-Object { $w = $_; -not ($out | Where-Object { $_ -like "*$w*" }) }
if ($miss) { Write-Host "FAIL: -Config -> missing $($miss -join ' | ')`n$($out -join "`n")"; $fail++ } else { Write-Host "ok: -Config -> $t\b\windows\play.json" }
if (Test-Path "$t\new save") { Write-Host "FAIL: -Config -DryRun made the save folder"; $fail++ }
$out = powershell -NoProfile -ExecutionPolicy Bypass -File $play -Config "$t\b\windows\play.json" -Save "$t\save" -DryRun 2>&1
if (-not ($out | Where-Object { $_ -like "*--save $t\save *" })) { Write-Host "FAIL: -Save over -Config`n$($out -join "`n")"; $fail++ } else { Write-Host "ok: -Save wins over play.json" }
# Interact everywhere: -Interact with Link Play sets DM_COOP_INTERACT; without Link Play it is refused
$out = powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$play' $a -LinkPlay -Interact" 2>&1
if (-not ($out | Where-Object { $_ -like "*DM_COOP=auto DM_COOP_INTERACT=1*" })) { Write-Host "FAIL: -LinkPlay -Interact`n$($out -join "`n")"; $fail++ } else { Write-Host "ok: -LinkPlay -Interact" }
$ErrorActionPreference = "Continue"   # the refusal is an error on stderr: read it, do not stop here
$out = powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$play' $a -Interact" 2>&1
$ErrorActionPreference = "Stop"
if (-not ($out | Where-Object { $_ -like "*-Interact goes with Link Play*" })) { Write-Host "FAIL: -Interact alone`n$($out -join "`n")"; $fail++ } else { Write-Host "ok: -Interact alone is refused" }
# The keyboard map: a DM_KEYMAP given from outside reaches the game unchanged
$env:DM_KEYMAP = "$t\controls.json"
$out = powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$play' $a" 2>&1
Remove-Item env:DM_KEYMAP
if (-not ($out | Where-Object { $_ -like "*keys: DM_KEYMAP=$t\controls.json*" })) { Write-Host "FAIL: DM_KEYMAP`n$($out -join "`n")"; $fail++ } else { Write-Host "ok: DM_KEYMAP" }
Remove-Item env:NXRT_TEST_PLAY
Remove-Item -Recurse -Force $t
if ($fail) { Write-Host "scripts.play_defaults: $fail failed"; exit 1 }
Write-Host "scripts.play_defaults: ok"
