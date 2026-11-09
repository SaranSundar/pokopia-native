# split.ps1: split-screen co-op, two players on one PC (docs/split-screen.md). Two games side by side with Link Play between them over this PC (loopback):
#   window 1 hosts with the player's save and takes the keyboard, the mouse and the first controller;
#   window 2 visits with the split-screen save and plays the second controller, muted (-Sound2: with sound).
# The split-screen save is made from the player's save the first time (scripts/splitsave.ps1: a copy with its own user ID, so the game lets it in) and kept;
# the player's save is only read. Interact everywhere is on in both (in a story town the second player can do little without it); -Story turns Story co-op on.
# -Keyboard: player 1 plays the keyboard and mouse only, player 2 the first controller. -Layout stack: above each other. Closing window 1 closes window 2.
# In the games: window 1 opens Link Play (Invite Others to Visit, Local connection); window 2 visits it (Visit a Friend, Local connection).
# The paths are play.ps1's (-Config play.json, or -ExeFS / -Dll / -RomFS / -Save / -Work / -Exe / -Shaders); -SplitSave names the second save's folder.
param(
    [string]$Config = "", [string]$ExeFS = "", [string]$Dll = "", [string]$RomFS = "", [string]$Save = "", [string]$Work = "", [string]$Exe = "", [string]$Shaders = "",
    [string]$SplitSave = "", [int]$Port = 47660, [switch]$Story, [switch]$Keyboard, [ValidateSet("side", "stack")][string]$Layout = "side", [switch]$Sound2, [switch]$DryRun
)
$ErrorActionPreference = "Stop"
$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
. (Join-Path $here "splitsave.ps1")
$play = Join-Path $here "play.ps1"

if ($Config) {   # the save and work folders as play.ps1 reads them
    $cfgDir = Split-Path -Parent (Resolve-Path -LiteralPath $Config).Path; $c = Get-Content -Raw -LiteralPath $Config | ConvertFrom-Json
    function CfgPath([string]$v) { if (-not $v) { return "" }; $v = [Environment]::ExpandEnvironmentVariables($v); if ([System.IO.Path]::IsPathRooted($v)) { $v } else { [System.IO.Path]::GetFullPath((Join-Path $cfgDir $v)) } }
    if (-not $Save) { $Save = CfgPath $c.save }
    if (-not $Work) { $Work = CfgPath $c.work }
}
if (-not $Save) { Write-Error "-Save (the player's save folder) or -Config is needed"; exit 2 }
if (-not $Work) { $la = $env:LOCALAPPDATA; if (-not $la) { $la = [Environment]::GetFolderPath('LocalApplicationData') }; $Work = Join-Path $la "pokopia-native" }
$Save = [System.IO.Path]::GetFullPath($Save); $Work = [System.IO.Path]::GetFullPath($Work)
if (-not $SplitSave) { $SplitSave = "$($Save.TrimEnd('\')) (split screen)" }
$Work2 = Join-Path $Work "split-screen"

# The second player's save: made once, then kept
if ($DryRun) {
    $made = if (Test-Path -LiteralPath (Join-Path $SplitSave "S00\SYDAT.BIN")) { "kept" } else { "would be made from $Save" }
    Write-Host "split-screen save: $SplitSave ($made)"
} else {
    try { $r = New-SplitSave $Save $SplitSave } catch { Write-Error $_.Exception.Message; exit 2 }
    Write-Host ("split-screen save {0}: {1}" -f $(if ($r.made) { "made (a copy of $Save with its own user ID)" } else { "kept" }), $SplitSave)
    $c1 = Join-Path $Work "cache"; $c2 = Join-Path $Work2 "cache"
    if ((Test-Path -LiteralPath $c1) -and -not (Test-Path -LiteralPath $c2)) {   # window 2 starts with window 1's shaders and pipelines, not from nothing
        New-Item -ItemType Directory -Force $c2 | Out-Null; Copy-Item -Path (Join-Path $c1 "*") -Destination $c2 -Recurse -Force
        Write-Host "window 2's shader cache: a copy of window 1's"
    }
}

function Quote([string]$a) { if ($a -match '[\s"]') { '"' + ($a -replace '"', '\"') + '"' } else { $a } }
$common = @()
foreach ($n in "Config", "ExeFS", "Dll", "RomFS", "Exe", "Shaders") { $v = Get-Variable $n -ValueOnly; if ($v) { $common += "-$n", $v } }
$common += "-Interact"; if ($Story) { $common += "-Story" }
$a1 = $common + @("-Save", $Save, "-Work", $Work, "-CoopHost", "$Port")
$a2 = $common + @("-Save", $SplitSave, "-Work", $Work2, "-CoopJoin", "127.0.0.1:$Port"); if (-not $Sound2) { $a2 += "-Silent" }
$pad1 = if ($Keyboard) { "0" } else { "1" }; $pad2 = if ($Keyboard) { "1" } else { "2" }
$env1 = @{ DM_SPLIT = "1"; DM_SPLIT_PAD = $pad1; DM_SPLIT_LAYOUT = $Layout; NXRT_VK_WINDOW = "960x540" }   # a window size keeps play.ps1 from going fullscreen
$env2 = @{ DM_SPLIT = "2"; DM_SPLIT_PAD = $pad2; DM_SPLIT_LAYOUT = $Layout; NXRT_VK_WINDOW = "960x540" }
if ($env:SPLIT_CTL_DIR) {   # tests: each window's DM_CTL file (keys and screenshots), ctl-1.txt and ctl-2.txt there
    $env1.DM_CTL = Join-Path $env:SPLIT_CTL_DIR "ctl-1.txt"; $env2.DM_CTL = Join-Path $env:SPLIT_CTL_DIR "ctl-2.txt"; $env1.NXRT_VK_SHOT_WINDOW = "1"; $env2.NXRT_VK_SHOT_WINDOW = "1"
}

function Start-Window($n, $args_, $envs) {
    $line = "-NoProfile -ExecutionPolicy Bypass -File $(Quote $play) " + (($args_ | ForEach-Object { Quote ([string]$_) }) -join " ")
    if ($DryRun) { Write-Host "window $n`: $(($envs.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ' ') powershell $line"; return $null }
    $old = @{}; foreach ($k in $envs.Keys) { $old[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, $envs[$k]) }
    try { return Start-Process powershell -ArgumentList $line -PassThru -WindowStyle Minimized } finally { foreach ($k in $old.Keys) { [Environment]::SetEnvironmentVariable($k, $old[$k]) } }
}

Write-Host "split screen: window 1 hosts (port $Port), window 2 visits; $(if ($Keyboard) { 'player 1: keyboard and mouse, player 2: the first controller' } else { 'player 1: keyboard, mouse and the first controller, player 2: the second controller' })"
$p1 = Start-Window 1 $a1 $env1
if (-not $DryRun) { Start-Sleep -Seconds 3 }
$p2 = Start-Window 2 $a2 $env2
if ($DryRun) { exit 0 }
Write-Host "In the games: window 1, PC -> Link Play -> Invite Others to Visit -> Local connection; window 2, PC -> Link Play -> Visit a Friend -> Local connection."
$p1.WaitForExit()
# window 1 closed: close window 2 too (its game, found by the split-screen save on its command line)
$g2 = Get-CimInstance Win32_Process -Filter "Name='nxrt.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($SplitSave) }
foreach ($g in $g2) { $pr = Get-Process -Id $g.ProcessId -ErrorAction SilentlyContinue; if ($pr) { [void]$pr.CloseMainWindow() } }
if ($p2) { [void]$p2.WaitForExit(15000) }
exit $p1.ExitCode
