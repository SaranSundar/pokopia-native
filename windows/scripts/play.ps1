# play.ps1: plays Pokemon Pokopia 2.0.1 with nxrt (the Vulkan renderer, SDL3 input and sound). See docs/m6-input-gameplay.md for the controls.
#   play.ps1 -ExeFS <ExeFS folder> -Dll <game.dll> -RomFS <RomFS.img or folder> -Save <save folder> [-Work <folder>] [-Silent] [-Fullscreen] [-Window 1600x900] [-Exe <nxrt.exe>]
#            [-LinkPlay] [-Join <IPv4[:port]>|ask] [-Internet [-Room <code>|ask] [-Relay <IPv4[:port]>]] [-CoopHost <port> | -CoopJoin <address:port> [-CoopTcp]] [-Interact]
#            [-PadCheck] [-DryRun]
#   play.ps1 -Config <play.json> [any switch above]: an installer build's launcher (docs/M7-REPORT.md): the paths come from play.json, relative ones from its folder,
#            %VARIABLES% expanded; a switch given here wins over the file. A save folder named only by the file is created empty the first time.
# The same script plays 1.0.5 and 2.0.1: -ExeFS, -Dll and -RomFS name one version's files (docs/m6-phase-b.md §1). -PadCheck logs every controller's buttons,
# sticks, gyro and rumble and shows them in the window title (docs/m6-phase-b.md §4). -DryRun prints the command line and starts nothing.
# -Save is the save the game plays on and writes to (the B00 / G00 / S00 folders; the game commits through a work copy beside it, <Save>.work). Point it at a
# copy, never at your only save. -Work holds the shader / pipeline cache (kept between runs: later starts have no compile hitches), temp files and the logs
# (default %LOCALAPPDATA%\pokopia-native). nxrt runs at below-normal priority. Nothing here names a game file: every path comes from the arguments.
# Shaders (an installer build): -Shaders <nxshader cache> (NXRT_SHADER_CACHE), -ShaderBuild <build folder> (NXRT_SHADER_BUILD: its hand-made answers), and for the
# run-time shaders -ShaderTool <nxshader.py> with -Python, -Installer <the installer's lowering steps> and -SpirvTools <folder> (NXRT_SHADER_TOOL, NXSHADER_TOOLS);
# -Fonts <folder> (DM_FONT_DIR), -PipelineSeed <list> (NXRT_VK_PIPELINE_SEED).
# Link Play "Local connection" between our builds (docs/coop-step1c.md, docs/coop-step1d.md): -LinkPlay hosts and finds hosts on this network; -Join <IPv4>[:<port>]
# visits one host by address instead; -Internet plays across the internet: every player gives the host's room code (-Room), and a host without one gets a new
# code shown in this window; -Relay <IPv4>[:<port>] goes through a relay you trust. "ask" for -Join or -Room asks in this window. For tests on one PC:
# -CoopHost <port> hosts on 127.0.0.1:<port>, -CoopJoin 127.0.0.1:<port> joins it; -CoopTcp sends the game's datagrams over the TCP link (through a tunnel).
# Each player needs a save of their own (the game refuses a second copy of the host's save). A second instance on one PC needs its own -Save and -Work.
# -Interact turns on "Interact everywhere" (experimental, docs/coop-interact.md): in every town, not only Palette Town, Link Play visitors can use moves and
# items and build. Every player in a session needs it on (a session refuses a player whose setting differs).
# The Link Play window (an installer build's launcher, or -Ask, when no Link Play switch is given): play alone, on this network, host or visit over the
# internet, remembered in %APPDATA%\Pokopia Native\linkplay.json. "Don't ask again" starts the remembered way; Shift held while starting brings it back.
# A host over the internet gets its room code in a window with a Copy button.
# The window's Controls tab sets this PC's keyboard keys (%APPDATA%\Pokopia Native\controls.json, handed to the game as DM_KEYMAP; the game logs the map):
# click an action, press its key. Keys count by their place on the keyboard; "Reset to defaults" brings back the usual layout. Controllers keep their buttons.
param(
    [string]$ExeFS = "",
    [string]$Dll = "",
    [string]$RomFS = "",
    [string]$Save = "",
    [string]$Work = "",
    [string]$Exe = "",
    [string]$Config = "",
    [string]$Shaders = "",
    [string]$ShaderBuild = "",
    [string]$ShaderTool = "",
    [string]$Python = "",
    [string]$Installer = "",
    [string]$SpirvTools = "",
    [string]$Fonts = "",
    [string]$PipelineSeed = "",
    [string]$Window = "",
    [switch]$Silent,
    [switch]$Fullscreen,
    [switch]$Ask,
    [switch]$LinkPlay,
    [string]$Join = "",
    [switch]$Internet,
    [string]$Room = "",
    [string]$Relay = "",
    [string]$CoopHost = "",
    [string]$CoopJoin = "",
    [switch]$CoopTcp,
    [switch]$Interact,
    [switch]$PadCheck,
    [switch]$DryRun
)
$ErrorActionPreference = "Stop"
$newSave = $false
if ($Config) {
    if (-not (Test-Path -LiteralPath $Config)) { Write-Error "not found: $Config"; exit 2 }
    $cfgDir = Split-Path -Parent (Resolve-Path -LiteralPath $Config).Path
    $c = Get-Content -Raw -LiteralPath $Config | ConvertFrom-Json
    function CfgPath([string]$v) {
        if (-not $v) { return "" }
        $v = [Environment]::ExpandEnvironmentVariables($v)
        if ([System.IO.Path]::IsPathRooted($v)) { return $v } else { return [System.IO.Path]::GetFullPath((Join-Path $cfgDir $v)) }
    }
    if (-not $ExeFS) { $ExeFS = CfgPath $c.exefs }
    if (-not $Dll) { $Dll = CfgPath $c.dll }
    if (-not $RomFS) { $RomFS = CfgPath $c.romfs }
    if (-not $Save) { $Save = CfgPath $c.save; $newSave = $true }
    if (-not $Work) { $Work = CfgPath $c.work }
    if (-not $Exe) { $Exe = CfgPath $c.exe }
    if (-not $Shaders) { $Shaders = CfgPath $c.shaders }
    if (-not $ShaderBuild) { $ShaderBuild = CfgPath $c.shader_build }
    if (-not $ShaderTool) { $ShaderTool = CfgPath $c.shader_tool }
    if (-not $Python) { $Python = CfgPath $c.python }
    if (-not $Installer) { $Installer = CfgPath $c.installer }
    if (-not $SpirvTools) { $SpirvTools = CfgPath $c.spirv_tools }
    if (-not $Fonts) { $Fonts = CfgPath $c.fonts }
    if (-not $PipelineSeed) { $PipelineSeed = CfgPath $c.pipeline_seed }
}
foreach ($n in @("ExeFS", "Dll", "RomFS", "Save")) { if (-not (Get-Variable $n -ValueOnly)) { Write-Error "-$n is needed (or -Config <play.json>)"; exit 2 } }
# The defaults are resolved here, not in param(): some hosts leave $PSScriptRoot empty there (a script block, -Command, older shells), so -Exe was needed
$here = $PSScriptRoot
if (-not $here -and $PSCommandPath) { $here = Split-Path -Parent $PSCommandPath }
if (-not $here -and $MyInvocation.MyCommand.Path) { $here = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Exe) {
    $look = @(); if ($here) { $look += Join-Path $here "..\build\Release\bin\nxrt.exe" }
    $look += (Join-Path (Get-Location).Path "build\Release\bin\nxrt.exe"), (Join-Path (Get-Location).Path "..\build\Release\bin\nxrt.exe")
    $Exe = $look | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $Exe) { Write-Error "nxrt.exe not found ($($look -join ', ')): give -Exe"; exit 2 }
}
if (-not $Work) {
    $la = $env:LOCALAPPDATA; if (-not $la) { $la = [Environment]::GetFolderPath('LocalApplicationData') }
    $Work = Join-Path $la "pokopia-native"
}
if (-not [System.IO.Path]::IsPathRooted($Work)) { $Work = Join-Path (Get-Location).Path $Work }   # nxrt starts in $Work: its paths must not be relative
if ($newSave -and -not (Test-Path -LiteralPath $Save) -and -not $DryRun) { New-Item -ItemType Directory -Force $Save | Out-Null; Write-Host "a new save folder: $Save" }
$must = @($ExeFS, $Dll, $RomFS, $Exe); if (-not ($newSave -and $DryRun)) { $must += $Save }   # a dry run does not make the new save folder
foreach ($p in $must) { if (-not (Test-Path -LiteralPath $p)) { Write-Error "not found: $p"; exit 2 } }
foreach ($m in @("main", "sdk")) { if (-not (Test-Path (Join-Path $ExeFS $m))) { Write-Error "$ExeFS has no '$m': give the game's ExeFS folder"; exit 2 } }
$noSave = -not (Get-ChildItem -LiteralPath $Save -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^[BGS]00$' })
if ($noSave) {
    Write-Warning "$Save holds no B00 / G00 / S00 folders: the game will start a new save there"
}
if ($CoopHost -and $CoopJoin) { Write-Error "-CoopHost and -CoopJoin: give one"; exit 2 }
if ($CoopHost -and $CoopHost -notmatch '^\d{1,5}$') { Write-Error "-CoopHost wants a port, e.g. 47600"; exit 2 }
if ($CoopJoin -and $CoopJoin -notmatch '^(127\.\d{1,3}\.\d{1,3}\.\d{1,3}|localhost):\d{1,5}$') {
    Write-Error "-CoopJoin wants 127.0.0.1:<port> (loopback only for now; reach another machine through an SSH tunnel to a local port)"; exit 2
}
$ipPort = '^(25[0-5]|2[0-4]\d|1?\d?\d)(\.(25[0-5]|2[0-4]\d|1?\d?\d)){3}(:\d{1,5})?$'
$roomRx = '^[0-9A-TV-Za-tv-z]{16}$'   # after dashes and spaces are taken out: Crockford's base32 (the game reads I, L and O as 1, 1 and 0)

function New-LinkPlayForm([string]$title) {
    $f = New-Object System.Windows.Forms.Form
    $f.Text = $title; $f.StartPosition = 'CenterScreen'; $f.FormBorderStyle = 'FixedDialog'; $f.MaximizeBox = $false; $f.MinimizeBox = $false
    $f.AutoScaleMode = 'Dpi'; $f.AutoSize = $true; $f.AutoSizeMode = 'GrowAndShrink'; $f.TopMost = $true; $f.ShowInTaskbar = $true
    $f.Font = New-Object System.Drawing.Font('Segoe UI', 10)
    $flow = New-Object System.Windows.Forms.FlowLayoutPanel
    $flow.FlowDirection = 'TopDown'; $flow.WrapContents = $false; $flow.AutoSize = $true; $flow.Padding = New-Object System.Windows.Forms.Padding(14)
    $f.Controls.Add($flow)
    return $f, $flow
}
function New-Label([string]$text, [int]$width = 470) {
    $l = New-Object System.Windows.Forms.Label; $l.Text = $text; $l.AutoSize = $true; $l.MaximumSize = New-Object System.Drawing.Size($width, 0); $l
}
function New-Buttons($form, [string[]]$names) {   # right-aligned; the first is the default (Enter), "Cancel" closes with Cancel
    $row = New-Object System.Windows.Forms.FlowLayoutPanel; $row.FlowDirection = 'RightToLeft'; $row.Size = New-Object System.Drawing.Size(470, 40)
    $row.Margin = New-Object System.Windows.Forms.Padding(0, 12, 0, 0)
    $out = @{}
    foreach ($n in $names) {
        $b = New-Object System.Windows.Forms.Button; $b.Text = $n; $b.AutoSize = $true; $b.MinimumSize = New-Object System.Drawing.Size(96, 30)
        $row.Controls.Add($b); $out[$n] = $b
    }
    $form.AcceptButton = $out[$names[0]]
    if ($out['Cancel']) { $out['Cancel'].DialogResult = 'Cancel'; $form.CancelButton = $out['Cancel'] }
    return $row, $out
}
# The keyboard's controls (the window's Controls tab, %APPDATA%\Pokopia Native\controls.json, given to the game as DM_KEYMAP; front_map.h front_keymap_parse):
# an action's key as an SDL scancode, the USB usage of the key's place on the keyboard, so a map means the same keys under any layout. The default is the
# game's own layout (WASD, J/K/U/I, ...)
$ControlsDefault = [ordered]@{ A = 13; B = 14; X = 24; Y = 12; L = 20; R = 8; ZL = 29; ZR = 6; Plus = 40; Minus = 43; StickUp = 26; StickLeft = 4; StickDown = 22
                               StickRight = 7; Up = 82; Down = 81; Left = 80; Right = 79; LS = 9; RS = 10; Shake = 44 }
$ControlsLabel = [ordered]@{ A = "A"; B = "B"; X = "X"; Y = "Y"; L = "L"; R = "R"; ZL = "ZL"; ZR = "ZR"; Plus = "+ (Plus)"; Minus = "- (Minus)"
                             StickUp = "Stick up"; StickLeft = "Stick left"; StickDown = "Stick down"; StickRight = "Stick right"; Up = "D-pad up"; Down = "D-pad down"
                             Left = "D-pad left"; Right = "D-pad right"; LS = "Left stick press"; RS = "Right stick press"; Shake = "Shake" }
# The keys a map can use: {set-1 scan code (0xE0.. for the extended ones): @(SDL scancode, name)}. Esc, F11, Alt and the Windows keys are the window's and Windows'
function Get-KeyTable {
    $t = @{}
    $rows = 'QWERTYUIOP', 'ASDFGHJKL', 'ZXCVBNM'; $first = 0x10, 0x1E, 0x2C
    for ($r = 0; $r -lt 3; $r++) { for ($i = 0; $i -lt $rows[$r].Length; $i++) { $ch = $rows[$r][$i]; $t[$first[$r] + $i] = @((4 + [int]$ch - [int][char]'A'), "$ch") } }
    for ($d = 1; $d -le 10; $d++) { $t[1 + $d] = @((29 + $d), "$($d % 10)") }   # 1..9, 0
    for ($n = 1; $n -le 10; $n++) { $t[0x3A + $n] = @((57 + $n), "F$n") }
    foreach ($m in @(@(0x58, 69, 'F12'), @(0x1C, 40, 'Enter'), @(0x0E, 42, 'Backspace'), @(0x0F, 43, 'Tab'), @(0x39, 44, 'Space'), @(0x0C, 45, '-'), @(0x0D, 46, '='),
                     @(0x1A, 47, '['), @(0x1B, 48, ']'), @(0x2B, 49, '\'), @(0x27, 51, ';'), @(0x28, 52, "'"), @(0x29, 53, '`'), @(0x33, 54, ','), @(0x34, 55, '.'),
                     @(0x35, 56, '/'), @(0x3A, 57, 'Caps Lock'), @(0x56, 100, '\ (ISO)'), @(0xE052, 73, 'Insert'), @(0xE047, 74, 'Home'), @(0xE049, 75, 'Page Up'),
                     @(0xE053, 76, 'Delete'), @(0xE04F, 77, 'End'), @(0xE051, 78, 'Page Down'), @(0xE04D, 79, 'Right arrow'), @(0xE04B, 80, 'Left arrow'),
                     @(0xE050, 81, 'Down arrow'), @(0xE048, 82, 'Up arrow'), @(0xE035, 84, 'Keypad /'), @(0x37, 85, 'Keypad *'), @(0x4A, 86, 'Keypad -'),
                     @(0x4E, 87, 'Keypad +'), @(0x4F, 89, 'Keypad 1'), @(0x50, 90, 'Keypad 2'), @(0x51, 91, 'Keypad 3'), @(0x4B, 92, 'Keypad 4'), @(0x4C, 93, 'Keypad 5'),
                     @(0x4D, 94, 'Keypad 6'), @(0x47, 95, 'Keypad 7'), @(0x48, 96, 'Keypad 8'), @(0x49, 97, 'Keypad 9'), @(0x52, 98, 'Keypad 0'), @(0x53, 99, 'Keypad .'),
                     @(0x1D, 224, 'Left Ctrl'), @(0x2A, 225, 'Left Shift'), @(0xE01D, 228, 'Right Ctrl'), @(0x36, 229, 'Right Shift'))) { $t[$m[0]] = @($m[1], $m[2]) }
    $t
}
# The saved map over the default (a missing or unreadable file, or an action it lacks, keeps the default)
function Read-Controls([string]$file) {
    $m = [ordered]@{}; foreach ($k in $ControlsDefault.Keys) { $m[$k] = $ControlsDefault[$k] }
    try {
        if (Test-Path -LiteralPath $file) {
            $j = Get-Content -Raw -LiteralPath $file | ConvertFrom-Json
            foreach ($k in @($m.Keys)) { $v = $j.$k -as [int]; if ($null -ne $v -and $v -ge 4 -and $v -le 231) { $m[$k] = $v } }
        }
    } catch {}
    $m
}
# How to play this time: alone, on this network, hosting or visiting over the internet; and the keyboard's controls. $pref holds the last choice, $keys the
# keyboard map; the new choice with the map in .controls, or $null (Cancel)
function Show-LinkPlay($pref, [string]$title, $keys) {
    $f, $outer = New-LinkPlayForm $title
    $addrRx = $ipPort; $codeRx = $roomRx; $labels = $ControlsLabel; $def = $ControlsDefault   # copied here: the event handlers' closures see this function's variables, not the script's
    $tabs = New-Object System.Windows.Forms.TabControl; $tabs.Margin = New-Object System.Windows.Forms.Padding(0)
    $pages = @{}
    foreach ($n in 'Link Play', 'Controls') {
        $p = New-Object System.Windows.Forms.TabPage; $p.Text = $n; $p.UseVisualStyleBackColor = $true
        $fl = New-Object System.Windows.Forms.FlowLayoutPanel; $fl.FlowDirection = 'TopDown'; $fl.WrapContents = $false; $fl.AutoSize = $true
        $fl.Padding = New-Object System.Windows.Forms.Padding(8); $p.Controls.Add($fl); [void]$tabs.TabPages.Add($p); $pages[$n] = $fl
    }
    $flow = $pages['Link Play']
    if ($script:noSave) {   # easy to miss: without Import save the game quietly starts a new one
        $ns = New-Label "No save here yet, so the game starts a new one. To play your save from Eden, the iPhone or the Mac, press Cancel and use Import save... in the builder first."
        $ns.ForeColor = [System.Drawing.Color]::DarkRed; $ns.Margin = New-Object System.Windows.Forms.Padding(0, 0, 0, 8); $flow.Controls.Add($ns)
    }
    $flow.Controls.Add((New-Label "How do you want to play?"))
    $modes = [ordered]@{ alone = "Play alone"; lan = "Link Play with players on this network (the same Wi-Fi)";
                        host = "Host over the internet (your router forwards port 47600 to this PC)"; visit = "Visit a host over the internet" }
    $radio = @{}
    $keep = New-Object System.Windows.Forms.CheckBox; $keep.AutoSize = $true; $keep.Margin = New-Object System.Windows.Forms.Padding(34, 2, 0, 0)
    $keep.Text = "Keep my room code from last time ($($pref.host_room))"; $keep.Checked = [bool]$pref.keep_room; $keep.Visible = [bool]$pref.host_room
    foreach ($k in $modes.Keys) {
        $r = New-Object System.Windows.Forms.RadioButton; $r.Text = $modes[$k]; $r.AutoSize = $true; $r.Checked = ($pref.mode -eq $k)
        $r.Margin = New-Object System.Windows.Forms.Padding(12, 4, 0, 0); $flow.Controls.Add($r); $radio[$k] = $r
        if ($k -eq 'host') { $flow.Controls.Add($keep) }   # under its own choice
    }
    if (-not ($radio.Values | Where-Object Checked)) { $radio['alone'].Checked = $true }
    $grid = New-Object System.Windows.Forms.TableLayoutPanel; $grid.ColumnCount = 2; $grid.AutoSize = $true; $grid.Margin = New-Object System.Windows.Forms.Padding(30, 4, 0, 0)
    $addr = New-Object System.Windows.Forms.TextBox; $addr.Width = 230; $addr.Text = $pref.join
    $code = New-Object System.Windows.Forms.TextBox; $code.Width = 230; $code.Text = $pref.room; $code.CharacterCasing = 'Upper'
    $grid.Controls.Add((New-Label "Host's address:" 160), 0, 0); $grid.Controls.Add($addr, 1, 0)
    $grid.Controls.Add((New-Label "Room code:" 160), 0, 1); $grid.Controls.Add($code, 1, 1)
    $flow.Controls.Add($grid)
    $ie = New-Object System.Windows.Forms.CheckBox; $ie.AutoSize = $true; $ie.Margin = New-Object System.Windows.Forms.Padding(0, 12, 0, 0)
    $ie.Text = "Interact everywhere (experimental)"; $ie.Checked = [bool]$pref.interact; $flow.Controls.Add($ie)
    $ieNote = New-Label "Visitors can use moves and items and build in every town, not only Palette Town. Every player needs it on." 450
    $ieNote.ForeColor = [System.Drawing.SystemColors]::GrayText; $ieNote.Margin = New-Object System.Windows.Forms.Padding(20, 0, 0, 0); $flow.Controls.Add($ieNote)
    $hint = New-Label "In the game: Link Play, then Local connection (not Online connection). Every player needs their own save and the same pokopia-native version."
    $hint.ForeColor = [System.Drawing.SystemColors]::GrayText; $hint.Margin = New-Object System.Windows.Forms.Padding(0, 8, 0, 0); $flow.Controls.Add($hint)
    # Controls: an action's button shows its key; click it, then press the new key. A key another action had moves over, and that action gets this one's old key
    $cf = $pages['Controls']
    $cf.Controls.Add((New-Label "Click an action, then press its key (Esc cancels). Keys count by their place on the keyboard; controllers keep their own buttons."))
    $st = @{ map = [ordered]@{}; act = $null; done = [datetime]::MinValue }
    foreach ($k in $keys.Keys) { $st.map[$k] = $keys[$k] }
    $tbl = Get-KeyTable; $names = @{}; foreach ($v in $tbl.Values) { $names[[int]$v[0]] = $v[1] }
    $cgrid = New-Object System.Windows.Forms.TableLayoutPanel; $cgrid.ColumnCount = 4; $cgrid.AutoSize = $true; $cgrid.Margin = New-Object System.Windows.Forms.Padding(0, 8, 0, 0)
    $kb = [ordered]@{}; $i = 0
    foreach ($a in $labels.Keys) {
        $b = New-Object System.Windows.Forms.Button; $b.Size = New-Object System.Drawing.Size(110, 28); $b.Margin = New-Object System.Windows.Forms.Padding(2, 2, 14, 2)
        $l = New-Label $labels[$a] 120; $l.Margin = New-Object System.Windows.Forms.Padding(0, 8, 4, 0)
        $cgrid.Controls.Add($l, ($i % 2) * 2, [math]::Floor($i / 2)); $cgrid.Controls.Add($b, ($i % 2) * 2 + 1, [math]::Floor($i / 2))
        $kb[$a] = $b; $i++
    }
    $cf.Controls.Add($cgrid)
    $status = New-Label " `n "; $status.ForeColor = [System.Drawing.SystemColors]::GrayText; $status.Margin = New-Object System.Windows.Forms.Padding(0, 8, 0, 0)
    $reset = New-Object System.Windows.Forms.Button; $reset.Text = "Reset to defaults"; $reset.AutoSize = $true; $reset.Margin = New-Object System.Windows.Forms.Padding(0, 8, 0, 0)
    $cf.Controls.Add($reset); $cf.Controls.Add($status)
    $show = { foreach ($a in $kb.Keys) { $v = [int]$st.map[$a]; $kb[$a].Text = if ($names[$v]) { $names[$v] } else { "(none)" } } }.GetNewClosure()
    & $show
    foreach ($a in @($kb.Keys)) {
        $act = $a
        $kb[$a].Add_Click({
            if (((Get-Date) - $st.done).TotalMilliseconds -lt 400) { return }   # the key that ended a capture can click the button it landed on
            & $show; $st.act = $act; $kb[$act].Text = "Press a key..."; $status.Text = "Press the key for $($labels[$act]) (Esc cancels)."
        }.GetNewClosure())
        $kb[$a].Add_PreviewKeyDown({ if ($st.act) { $_.IsInputKey = $true } }.GetNewClosure())   # Tab, Enter and the arrows reach KeyDown instead of moving the focus
    }
    $f.KeyPreview = $true
    $f.Add_KeyDown({
        if (-not $st.act) { return }
        $_.Handled = $true; $_.SuppressKeyPress = $true
        $act = $st.act; $st.act = $null; $st.done = Get-Date
        $vk = [int]$_.KeyCode
        if ($vk -eq 27) { & $show; $status.Text = "Cancelled."; return }
        if ($vk -eq 16) { $vk = if ([PokopiaKeys]::Down(0xA1)) { 0xA1 } else { 0xA0 } } elseif ($vk -eq 17) { $vk = if ([PokopiaKeys]::Down(0xA3)) { 0xA3 } else { 0xA2 } }
        $hit = if ($vk -in 18, 91, 92, 122) { $null } else { $tbl[[int][PokopiaKeys]::Scan($vk)] }
        if (-not $hit) { & $show; $status.Text = "That key can't be used: Esc, F11, Alt and the Windows keys belong to the game's window and to Windows."; return }
        $sdl = [int]$hit[0]; $old = $st.map[$act]; $msg = "$($labels[$act]): $($hit[1])."
        foreach ($o in @($st.map.Keys)) { if ($o -ne $act -and [int]$st.map[$o] -eq $sdl) { $st.map[$o] = $old; $msg = "$($labels[$act]): $($hit[1]), which $($labels[$o]) had; $($labels[$o]) takes the old key." } }
        $st.map[$act] = $sdl; & $show; $status.Text = $msg
    }.GetNewClosure())
    $reset.Add_Click({ foreach ($k in $def.Keys) { $st.map[$k] = $def[$k] }; $st.act = $null; & $show; $status.Text = "Back to the default keys." }.GetNewClosure())
    $w = [math]::Max($pages['Link Play'].PreferredSize.Width, $pages['Controls'].PreferredSize.Width) + 16
    $h = [math]::Max($pages['Link Play'].PreferredSize.Height, $pages['Controls'].PreferredSize.Height) + 16
    $tabs.Size = New-Object System.Drawing.Size($w, ($h + $tabs.ItemSize.Height + 20))
    $outer.Controls.Add($tabs)
    $again = New-Object System.Windows.Forms.CheckBox; $again.AutoSize = $true; $again.Margin = New-Object System.Windows.Forms.Padding(0, 12, 0, 0)
    $again.Text = "Don't ask again (hold Shift while starting to see this window)"; $again.Checked = -not $pref.ask
    $outer.Controls.Add($again)
    $row, $btn = New-Buttons $f @('Play', 'Cancel'); $row.Width = $w; $outer.Controls.Add($row)
    $sync = { $grid.Enabled = $radio['visit'].Checked; $keep.Enabled = $radio['host'].Checked; $ie.Enabled = -not $radio['alone'].Checked }.GetNewClosure()
    foreach ($r in $radio.Values) { $r.Add_CheckedChanged($sync) }
    & $sync
    $btn['Play'].Add_Click({
        if ($radio['visit'].Checked) {
            $a = $addr.Text.Trim(); $c = ($code.Text -replace '[-\s]', '')
            if ($a -notmatch $addrRx) { [void][System.Windows.Forms.MessageBox]::Show($f, "Type the host's internet address, for example 203.0.113.7.", $title); return }
            if ($c -notmatch $codeRx) { [void][System.Windows.Forms.MessageBox]::Show($f, "Type the host's room code: 16 letters and digits, XXXX-XXXX-XXXX-XXXX.", $title); return }
        }
        $f.DialogResult = 'OK'; $f.Close()
    }.GetNewClosure())
    $f.Add_Shown({ $f.Activate() }.GetNewClosure())
    if ($f.ShowDialog() -ne 'OK') { return $null }
    $mode = ($radio.Keys | Where-Object { $radio[$_].Checked } | Select-Object -First 1)
    $new = @{} + $pref
    $new.mode = $mode; $new.ask = -not $again.Checked; $new.keep_room = $keep.Checked; $new.interact = $ie.Checked
    if ($mode -eq 'visit') { $new.join = $addr.Text.Trim(); $new.room = $code.Text.Trim().ToUpper() }
    $new.controls = $st.map
    return $new
}
# A host's room code, big, with a Copy button: the players who visit need it (and this PC's internet address)
function Show-RoomCode([string]$code, [string]$title, [string]$relay) {
    $f, $flow = New-LinkPlayForm $title
    $flow.Controls.Add((New-Label "Your room code for this session. Give it to the players who visit:"))
    $box = New-Object System.Windows.Forms.TextBox; $box.ReadOnly = $true; $box.Text = $code; $box.TextAlign = 'Center'; $box.Width = 470
    $box.Font = New-Object System.Drawing.Font('Consolas', 22, [System.Drawing.FontStyle]::Bold); $box.Margin = New-Object System.Windows.Forms.Padding(0, 8, 0, 8)
    $flow.Controls.Add($box)
    if ($relay) { $flow.Controls.Add((New-Label "They also need the relay's address ($relay). In the game: Link Play, then Local connection, then host.")) }
    else {
        $flow.Controls.Add((New-Label ("They also need this PC's internet address (search ""what is my IP"" on this network), and your router must forward port " +
                                       "47600, TCP and UDP, to this PC. In the game: Link Play, then Local connection, then host.")))
    }
    $row, $btn = New-Buttons $f @('Start the game', 'Copy')
    $btn['Copy'].Add_Click({
        try { [System.Windows.Forms.Clipboard]::SetText($code); $btn['Copy'].Text = 'Copied' } catch { $btn['Copy'].Text = 'Copy failed: select it' }
    }.GetNewClosure())
    $btn['Start the game'].DialogResult = 'OK'
    $flow.Controls.Add($row)
    $f.Add_Shown({ $f.Activate(); $box.SelectAll() }.GetNewClosure())
    [void]$f.ShowDialog()
}

$windowed = $false   # the Link Play window chose this session's way: the room code goes in a window too
$keysFile = Join-Path ([Environment]::GetFolderPath('ApplicationData')) "Pokopia Native\controls.json"
if (($Config -or $Ask) -and -not ($LinkPlay -or $Join -or $Internet -or $Room -or $Relay -or $CoopHost -or $CoopJoin -or $env:DM_COOP) -and -not $DryRun) {
    $prefFile = Join-Path ([Environment]::GetFolderPath('ApplicationData')) "Pokopia Native\linkplay.json"
    $pref = @{ mode = "alone"; join = ""; room = ""; host_room = ""; keep_room = $false; ask = $true; interact = $false }
    try { if (Test-Path -LiteralPath $prefFile) { (Get-Content -Raw -LiteralPath $prefFile | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $pref[$_.Name] = $_.Value } } } catch {}
    try { Add-Type -AssemblyName System.Windows.Forms, System.Drawing; $forms = $true } catch { $forms = $false; Write-Warning "no Windows Forms: Link Play starts the remembered way" }
    if ($forms) {
        [System.Windows.Forms.Application]::EnableVisualStyles()
        $shift = ([System.Windows.Forms.Control]::ModifierKeys -band [System.Windows.Forms.Keys]::Shift) -ne 0
        if ($pref.ask -or $shift) {
            if (-not ('PokopiaKeys' -as [type])) {   # the Controls tab: a key's scan code (its place) and which Shift or Ctrl it was
                Add-Type -TypeDefinition ('using System.Runtime.InteropServices; public static class PokopiaKeys { [DllImport("user32.dll")] static extern uint MapVirtualKey(uint c, uint t); ' +
                    '[DllImport("user32.dll")] static extern short GetKeyState(int k); public static uint Scan(int vk) { return MapVirtualKey((uint)vk, 4); } ' +
                    'public static bool Down(int vk) { return (GetKeyState(vk) & 0x8000) != 0; } }')
            }
            $keys = Read-Controls $keysFile
            $new = Show-LinkPlay $pref "Pokopia" $keys
            if (-not $new) { Write-Host "cancelled"; exit 0 }
            $chosen = $new.controls; $new.Remove('controls')
            $pref = $new
            try { New-Item -ItemType Directory -Force (Split-Path -Parent $prefFile) | Out-Null; $pref | ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath $prefFile } catch {}
            if ($chosen.Keys | Where-Object { [int]$chosen[$_] -ne [int]$keys[$_] }) {   # changed in the window (Reset to defaults too): saved for this PC
                $o = [ordered]@{ version = 1 }; foreach ($k in $chosen.Keys) { $o[$k] = [int]$chosen[$k] }
                try { $o | ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath $keysFile } catch { Write-Warning "the controls were not saved: $keysFile" }
            }
        }
        $windowed = $true
    }
    switch ($pref.mode) {
        'lan' { $LinkPlay = $true }
        'host' { $Internet = $true; if ($pref.keep_room -and $pref.host_room) { $Room = $pref.host_room } }
        'visit' { $Internet = $true; $Join = $pref.join; $Room = $pref.room }
    }
    if ($pref.mode -ne 'alone' -and $pref.interact) { $Interact = $true }
}
if ($Join -or $Internet -or $Room -or $Relay) { $LinkPlay = $true }
if ($LinkPlay -and ($CoopHost -or $CoopJoin)) { Write-Error "-LinkPlay / -Join / -Internet and -CoopHost / -CoopJoin: give one kind"; exit 2 }
if ($LinkPlay) {
    if ($Join -eq "ask") { $Join = (Read-Host "The host's address (for example 203.0.113.7, or 192.168.1.20:47600)").Trim(); if (-not $Join) { Write-Error "no address given"; exit 2 } }
    if ($Join -and $Join -notmatch $ipPort) { Write-Error "-Join wants the host's IPv4 address, for example 203.0.113.7 or 192.168.1.20:47600 (got '$Join')"; exit 2 }
    if ($Room -eq "ask") { $Room = (Read-Host "The host's room code (XXXX-XXXX-XXXX-XXXX)").Trim(); if (-not $Room) { Write-Error "no room code given"; exit 2 } }
    if ($Room -and (($Room -replace '[-\s]', '') -notmatch '^[0-9A-TV-Za-tv-z]{16}$')) { Write-Error "-Room wants 16 letters and digits, XXXX-XXXX-XXXX-XXXX (got '$Room')"; exit 2 }
    if ($Relay -and -not $Internet) { Write-Error "-Relay goes with -Internet"; exit 2 }
    if ($Relay -and $Relay -notmatch $ipPort) { Write-Error "-Relay wants the relay's IPv4 address[:port] (got '$Relay')"; exit 2 }
    if ($Internet -and $Join -and -not $Room) { Write-Error "visiting over the internet needs the host's room code: -Room XXXX-XXXX-XXXX-XXXX (or -Room ask)"; exit 2 }
    if ($Internet -and -not $Room) {   # this PC hosts: a new code for this session, shown below (the game would only log its own)
        $b = New-Object byte[] 16; [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b)
        $abc = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"; $s = -join ($b | ForEach-Object { $abc[$_ % 32] })   # 256 is a multiple of 32: no bias
        $Room = "$($s.Substring(0, 4))-$($s.Substring(4, 4))-$($s.Substring(8, 4))-$($s.Substring(12, 4))"
    }
    $env:DM_COOP = "auto"
    if ($Join) { $env:DM_COOP_JOIN = $Join }
    if ($Internet) { $env:DM_COOP_INTERNET = "1"; $env:DM_COOP_ROOM = $Room.ToUpper() }
    if ($Relay) { $env:DM_COOP_RELAY = $Relay }
    Write-Host ""
    if ($Join) { Write-Host "Link Play: visiting $Join. In the game: Link Play, Local connection, then look for a town." }
    else { Write-Host "Link Play: on. In the game: Link Play, Local connection, to host or to look for towns$(if (-not $Internet) { ' on this network' })." }
    if ($Internet) {
        Write-Host "Internet play, room code: $($env:DM_COOP_ROOM)" -ForegroundColor Yellow
        if ($Relay) { Write-Host "Through the relay $Relay`: every player needs this code and the relay's address." }
        elseif (-not $Join) { Write-Host "Visitors need this code and this PC's internet address; your router must forward port 47600 (TCP and UDP) to this PC." }
        Write-Host "Nothing is encrypted: anyone on the way can read the game's traffic."
    }
    Write-Host ""
    if ($Internet -and -not $Join -and -not $DryRun) {   # this PC hosts over the internet: the room code in a window with a Copy button
        try {
            Add-Type -AssemblyName System.Windows.Forms, System.Drawing; [System.Windows.Forms.Application]::EnableVisualStyles()
            Show-RoomCode $env:DM_COOP_ROOM "Pokopia - your room code" $Relay
        } catch { Write-Warning "the room code window did not open: the code is above" }
        if ($windowed) {   # "Keep my room code" offers this one next time
            $pref.host_room = $env:DM_COOP_ROOM
            try { $pref | ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath $prefFile } catch {}
        }
    }
} elseif ($CoopHost) { $env:DM_COOP = "host:$CoopHost" } elseif ($CoopJoin) { $env:DM_COOP = "join:$CoopJoin" }
if ($CoopTcp) { $env:DM_COOP_TCP = "1" }
if ($Interact) {
    if (-not $env:DM_COOP) { Write-Error "-Interact goes with Link Play (-LinkPlay, -Join, -Internet, -CoopHost or -CoopJoin)"; exit 2 }
    $env:DM_COOP_INTERACT = "1"
    Write-Host "Interact everywhere: on (experimental). Every player needs it on; a session refuses a player whose setting differs."
}
if ($env:DM_COOP -and -not $LinkPlay) { Write-Host "Link Play: DM_COOP=$env:DM_COOP$(if ($env:DM_COOP_TCP) { ', datagrams over TCP' })" }
$cache = Join-Path $Work "cache"; $temp = Join-Path $Work "temp"; $logs = Join-Path $Work "logs"
if (-not $DryRun) { New-Item -ItemType Directory -Force $cache, $temp, $logs | Out-Null }

$env:NXRT_VK_CACHE_DIR = $cache            # shaders and pipelines, kept between runs
$env:DM_AUDIO = if ($Silent) { "0" } else { "1" }   # the sound card (SDL3); the silent sink stands in when there is none
if ($Window) { $env:NXRT_VK_WINDOW = $Window }
if ($Fullscreen) { $env:NXRT_VK_FULLSCREEN = "1" }
if ($PadCheck) { $env:DM_PAD_CHECK = "1"; $env:DM_RUMBLE_LOG = "1"; $env:DM_MOTION_LOG = "1" }   # the controller check (front_sdl.c), the game's vibration and motion (hle_hid.c)
if (-not $env:DM_KEYMAP -and (Test-Path -LiteralPath $keysFile)) { $env:DM_KEYMAP = $keysFile }   # this PC's controls (the window's Controls tab); the game logs the map it uses
if ($Shaders) {   # an installer build's shader cache (nxs_rt.c), and the run-time shaders translated into it on demand
    $env:NXRT_SHADER_CACHE = $Shaders
    if ($ShaderBuild) { $env:NXRT_SHADER_BUILD = $ShaderBuild }
    if ($SpirvTools) { $env:NXSHADER_TOOLS = $SpirvTools }
    if ($ShaderTool) {
        $py = $Python; $pyArg = ""
        if (-not $py -or -not (Test-Path -LiteralPath $py)) {   # the Python the build used has moved: the launcher's, else whatever is on the PATH
            $found = Get-Command py.exe, python.exe -ErrorAction SilentlyContinue | Select-Object -First 1
            $py = if ($found) { $found.Source } else { "" }; if ($found -and $found.Name -eq "py.exe") { $pyArg = " -3" }
        }
        if ($py -and [System.IO.Path]::GetFileName($py) -ieq "pythonw.exe") {   # a build made from the 1.1.0 window: under pythonw every console tool the translator runs opens a window
            $pc = Join-Path (Split-Path -Parent $py) "python.exe"; if (Test-Path -LiteralPath $pc) { $py = $pc }
        }
        if ($py) {
            $env:NXRT_SHADER_TOOL = "`"$py`"$pyArg -I `"$ShaderTool`" --cache `"$Shaders`"" + $(if ($Installer) { " --installer `"$Installer`"" } else { "" })
        } else { Write-Warning "no Python found: shaders the game makes while it runs cannot be translated (their draws are skipped)" }
    }
}
if ($Fonts) { $env:DM_FONT_DIR = $Fonts }
if ($PipelineSeed) { $env:NXRT_VK_PIPELINE_SEED = $PipelineSeed }
$args_ = @((Resolve-Path -LiteralPath $ExeFS).Path, "--dll", (Resolve-Path -LiteralPath $Dll).Path, "--romfs", (Resolve-Path -LiteralPath $RomFS).Path,
           "--save", [System.IO.Path]::GetFullPath($Save), "--temp", $temp, "--log", $logs, "--gfx", "vulkan")
if ($DryRun) {
    Write-Host "exe: $((Resolve-Path -LiteralPath $Exe).Path)"; Write-Host "work: $Work"; Write-Host "args: $($args_ -join ' ')"
    Write-Host "env: NXRT_VK_CACHE_DIR=$env:NXRT_VK_CACHE_DIR DM_AUDIO=$env:DM_AUDIO DM_PAD_CHECK=$env:DM_PAD_CHECK"
    if ($Shaders) { Write-Host "shaders: NXRT_SHADER_CACHE=$env:NXRT_SHADER_CACHE NXRT_SHADER_BUILD=$env:NXRT_SHADER_BUILD NXSHADER_TOOLS=$env:NXSHADER_TOOLS"; Write-Host "shader tool: $env:NXRT_SHADER_TOOL" }
    if ($Fonts -or $PipelineSeed) { Write-Host "fonts: $env:DM_FONT_DIR; pipeline seed: $env:NXRT_VK_PIPELINE_SEED" }
    if ($env:DM_COOP) { Write-Host "link play: DM_COOP=$env:DM_COOP DM_COOP_INTERACT=$env:DM_COOP_INTERACT" }
    if ($env:DM_KEYMAP) { Write-Host "keys: DM_KEYMAP=$env:DM_KEYMAP" }
    exit 0
}
Write-Host "Pokopia: keyboard WASD move, mouse look (click the picture; Esc releases the mouse), J/K/U/I = A/B/X/Y, Return = +, Tab = -, F11 fullscreen."
Write-Host "Controllers work as Switch controllers (Xbox / PlayStation / Switch Pro). Save: $Save. Logs: $logs"
# Start-Process quotes nothing: an argument with a space (a save under "Pokopia Native") is passed in quotes, an inner quote escaped
$quoted = $args_ | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }
$p = Start-Process -FilePath (Resolve-Path -LiteralPath $Exe).Path -ArgumentList $quoted -WorkingDirectory $Work -PassThru
try { $p.PriorityClass = 'BelowNormal' } catch {}
$p.WaitForExit()
exit $p.ExitCode
