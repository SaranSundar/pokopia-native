# splitsave.ps1: the split-screen save (docs/split-screen.md). Dot-source it for New-SplitSave and Get-SaveId, or run it:
#   splitsave.ps1 -From <the player's save folder> -To <the split-screen save folder>
# The game refuses a second copy of a save in its own session (error 9005, "a user with the same user ID is already in the session"; docs/coop-step1.md):
# what it compares is the 16-byte user ID in S00\SYDAT.BIN at 0x61, a file without a checksum. So the second player's save is a copy of the player's with
# only that ID changed, made once and kept (the second player's own pickups stay in it). The player's save is only read. The new ID is derived from the
# old one (SHA-256 of a fixed text and it), so a split save made again from the same save is the same player to the game.
param([string]$From = "", [string]$To = "")

$SydatMagic = [uint32]0x31227687   # 87 76 22 31, the first word of every save file of the game
$IdAt = 0x61

function Get-SaveId([string]$sydat) {
    $b = [System.IO.File]::ReadAllBytes($sydat)
    if ($b.Length -lt $IdAt + 16 -or [BitConverter]::ToUInt32($b, 0) -ne $SydatMagic) { throw "$sydat is not the game's system save (S00\SYDAT.BIN)" }
    return (($b[$IdAt..($IdAt + 15)] | ForEach-Object { $_.ToString("x2") }) -join "")
}

function New-SplitId([byte[]]$old) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { $h = $sha.ComputeHash([byte[]]([System.Text.Encoding]::ASCII.GetBytes("pokopia-native split screen, player 2") + $old)) } finally { $sha.Dispose() }
    $new = [byte[]]$h[0..15]
    if (-not (Compare-Object $new $old)) { $new[0] = $new[0] -bxor 0xff }   # never the same as the player's
    return $new
}

# Returns @{ made = $true when this call made it; path; id; from_id }. Throws when the player's save has no system save yet.
function New-SplitSave([string]$From, [string]$To) {
    if (-not $From -or -not $To) { throw "New-SplitSave needs -From and -To" }
    $From = [System.IO.Path]::GetFullPath($From); $To = [System.IO.Path]::GetFullPath($To)
    if ($From.TrimEnd('\') -ieq $To.TrimEnd('\')) { throw "the split-screen save must be another folder than the player's" }
    $src = Join-Path $From "S00\SYDAT.BIN"
    if (-not (Test-Path -LiteralPath $src)) { throw "no S00\SYDAT.BIN in $From`: play once and save, then start split screen" }
    $fromId = Get-SaveId $src
    $dst = Join-Path $To "S00\SYDAT.BIN"
    if (Test-Path -LiteralPath $dst) {   # made before: kept as it is
        $id = Get-SaveId $dst
        if ($id -eq $fromId) { throw "$To has the player's own user ID: it is a copy, not a split-screen save (the game would refuse it)" }
        return @{ made = $false; path = $To; id = $id; from_id = $fromId }
    }
    if ((Test-Path -LiteralPath $To) -and (Get-ChildItem -LiteralPath $To -Force | Select-Object -First 1)) { throw "$To exists and holds no save: move it away first" }
    $tmp = "$To.making"
    if (Test-Path -LiteralPath $tmp) { Remove-Item -Recurse -Force -LiteralPath $tmp }
    New-Item -ItemType Directory -Force $tmp | Out-Null
    Get-ChildItem -LiteralPath $From -Force | Copy-Item -Destination $tmp -Recurse -Force   # the whole save, read only
    $b = [System.IO.File]::ReadAllBytes((Join-Path $tmp "S00\SYDAT.BIN"))
    $new = New-SplitId ([byte[]]$b[$IdAt..($IdAt + 15)])
    [Array]::Copy($new, 0, $b, $IdAt, 16)
    [System.IO.File]::WriteAllBytes((Join-Path $tmp "S00\SYDAT.BIN"), $b)
    if (Test-Path -LiteralPath $To) { Remove-Item -Force -LiteralPath $To }   # the empty folder
    Move-Item -LiteralPath $tmp -Destination $To
    return @{ made = $true; path = $To; id = (Get-SaveId $dst); from_id = $fromId }
}

if ($MyInvocation.InvocationName -ne '.' -and ($From -or $To)) {   # run as a command, not dot-sourced
    try { $r = New-SplitSave $From $To } catch { Write-Error $_.Exception.Message; exit 2 }
    Write-Host ("split-screen save {0}: {1}" -f $(if ($r.made) { "made" } else { "kept" }), $r.path)
}
