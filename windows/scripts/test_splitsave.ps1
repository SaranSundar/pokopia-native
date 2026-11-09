# test_splitsave.ps1 (ctest scripts.split_save): New-SplitSave (splitsave.ps1) on stand-in saves: the copy differs from the player's save only in the user ID,
# the player's save stays as it was, the split save is made once and then kept, the same save gives the same ID again, and the cases it must refuse
param([string]$Repo = (Join-Path $PSScriptRoot ".."))
$ErrorActionPreference = "Stop"
. (Join-Path $Repo "scripts\splitsave.ps1")
$t = Join-Path ([System.IO.Path]::GetTempPath()) ("nxrt-test-split-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
$fail = 0
function Ok($cond, $what) { if ($cond) { Write-Host "ok: $what" } else { Write-Host "FAIL: $what"; $script:fail++ } }
function Throws($block, $what) { try { & $block; Write-Host "FAIL: $what (no error)"; $script:fail++ } catch { Write-Host "ok: $what ($($_.Exception.Message))" } }
function Hashes($dir) { Get-ChildItem -LiteralPath $dir -Recurse -File | Sort-Object FullName | ForEach-Object { "$($_.FullName.Substring($dir.Length)) $((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)" } }
function New-StandIn($dir, [byte]$seed) {   # the layout of a real save: B00 / G00 / S00; SYDAT with the magic, a name at 0x40 and the ID at 0x61
    foreach ($d in "B00", "G00", "S00") { New-Item -ItemType Directory -Force (Join-Path $dir $d) | Out-Null }
    $b = New-Object byte[] 2009; [Array]::Copy([byte[]](0x87, 0x76, 0x22, 0x31, 0x3b, 1, 0, 1, 1), $b, 9)
    [Array]::Copy([System.Text.Encoding]::UTF8.GetBytes("Tester"), 0, $b, 0x40, 6)
    for ($i = 0; $i -lt 16; $i++) { $b[0x61 + $i] = [byte](($seed + 7 * $i) -band 0xff) }
    for ($i = 0x71; $i -lt $b.Length; $i++) { $b[$i] = [byte](($i * 13) -band 0xff) }
    [System.IO.File]::WriteAllBytes((Join-Path $dir "S00\SYDAT.BIN"), $b)
    foreach ($f in "G00\CMNDAT.BIN", "G00\STGDAT01.BIN", "B00\CMNDAT.BIN") { [System.IO.File]::WriteAllBytes((Join-Path $dir $f), [byte[]](0x87, 0x76, 0x22, 0x31) + [byte[]](1..200)) }
}
try {
    $p = Join-Path $t "save"; $s = Join-Path $t "save (split screen)"
    New-StandIn $p 0x63
    $before = Hashes $p
    $r = New-SplitSave $p $s
    Ok ($r.made) "made the first time"
    Ok ($r.id -ne $r.from_id -and $r.id.Length -eq 32) "a new user ID ($($r.from_id) -> $($r.id))"
    Ok (-not (Compare-Object $before (Hashes $p))) "the player's save is unchanged"
    $a = [System.IO.File]::ReadAllBytes("$p\S00\SYDAT.BIN"); $b = [System.IO.File]::ReadAllBytes("$s\S00\SYDAT.BIN")
    $diff = @(for ($i = 0; $i -lt $a.Length; $i++) { if ($a[$i] -ne $b[$i]) { $i } })
    Ok ($a.Length -eq $b.Length -and $diff.Count -gt 0 -and ($diff | Where-Object { $_ -lt 0x61 -or $_ -gt 0x70 }).Count -eq 0) "SYDAT differs only in the ID's 16 bytes"
    $others = @(Hashes $p | Where-Object { $_ -notlike "\S00\SYDAT.BIN *" }); $sothers = @(Hashes $s | Where-Object { $_ -notlike "\S00\SYDAT.BIN *" })
    Ok (-not (Compare-Object $others $sothers)) "every other file is a byte-for-byte copy"
    Ok (-not (Test-Path "$s.making")) "no half-made folder left"
    $r2 = New-SplitSave $p $s
    Ok (-not $r2.made -and $r2.id -eq $r.id) "kept the second time, the same ID"
    [System.IO.File]::WriteAllBytes("$s\G00\CMNDAT.BIN", [byte[]](9, 9, 9)); $r3 = New-SplitSave $p $s
    Ok (-not $r3.made -and [System.IO.File]::ReadAllBytes("$s\G00\CMNDAT.BIN").Length -eq 3) "the second player's progress is kept, not copied over"
    Remove-Item -Recurse -Force $s; $r4 = New-SplitSave $p $s
    Ok ($r4.made -and $r4.id -eq $r.id) "made again from the same save: the same ID (the same second player)"
    $q = Join-Path $t "other"; New-StandIn $q 0x11; $rq = New-SplitSave $q (Join-Path $t "other split")
    Ok ($rq.id -ne $r.id -and $rq.id -ne $rq.from_id) "another save: another ID"
    $e = Join-Path $t "empty"; New-Item -ItemType Directory -Force $e | Out-Null
    Throws { New-SplitSave $e (Join-Path $t "e2") } "refuses a save folder without S00\SYDAT.BIN (a new game)"
    Throws { New-SplitSave $p $p } "refuses the player's own folder as the split save"
    $c = Join-Path $t "plain copy"; Copy-Item -Recurse $p $c
    Throws { New-SplitSave $p $c } "refuses a plain copy (the player's own ID)"
    $j = Join-Path $t "junk"; New-Item -ItemType Directory -Force $j | Out-Null; Set-Content "$j\x.txt" "x"
    Throws { New-SplitSave $p $j } "refuses a folder that holds something else"
    [System.IO.File]::WriteAllBytes("$e\x", [byte[]](1)); New-Item -ItemType Directory -Force "$e\S00" | Out-Null; [System.IO.File]::WriteAllBytes("$e\S00\SYDAT.BIN", [byte[]](1, 2, 3))
    Throws { New-SplitSave $e (Join-Path $t "e3") } "refuses a SYDAT.BIN without the game's magic"
} finally { Remove-Item -Recurse -Force -LiteralPath $t -ErrorAction SilentlyContinue }
if ($fail) { Write-Host "$fail failed"; exit 1 }
Write-Host "all passed"
