#Requires -Version 7.4
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Prepare = Join-Path $PSScriptRoot '../../../../scripts/multiplayer/windows/PrepareW1Fixture.ps1'
$Temp = Join-Path ([IO.Path]::GetTempPath()) ('w1-fixture-' + [guid]::NewGuid().ToString('N'))
$Editor = Join-Path $Temp 'editor'
$Game = Join-Path $Temp 'game'
$Source = Join-Path $Editor 'gamedata/levels/w1_arena'
$Target = Join-Path $Game 'gamedata/levels/w1_arena'
$Failures = [System.Collections.Generic.List[string]]::new()

function Assert-Result([bool] $Condition, [string] $Description) {
    if (!$Condition) { $Failures.Add($Description) }
}

function ConvertTo-Chunk([uint32] $Id, [byte[]] $Body) {
    return ,([byte[]] ([BitConverter]::GetBytes($Id) + [BitConverter]::GetBytes([uint32] $Body.Length) + $Body))
}

function Set-RespawnPoints([byte] $SecondTeam, [uint16] $SecondMode, [float] $SecondX, [uint32] $SecondId = 1) {
    # The record is six floats, one byte each for team and spawn kind, a u16 mode and a NUL.
    $First = [byte[]]::new(29)
    $First[26] = 2
    $Second = [byte[]] $First.Clone()
    [BitConverter]::GetBytes($SecondX).CopyTo($Second, 0)
    $Second[24] = $SecondTeam
    [BitConverter]::GetBytes($SecondMode).CopyTo($Second, 26)
    $Records = (ConvertTo-Chunk 0 $First) + (ConvertTo-Chunk $SecondId $Second)
    [IO.File]::WriteAllBytes((Join-Path $Source 'level.game'), (ConvertTo-Chunk 0x2000 $Records))
}

function Assert-Rejected([string] $Text) {
    $ErrorText = ''
    try { $null = & $Prepare -EditorRoot $Editor -GameDataRoot $Game }
    catch { $ErrorText = $_.Exception.Message }
    Assert-Result ($ErrorText -like "*$Text*") "Expected an error containing '$Text', received '$ErrorText'."
    Assert-Result (!(Test-Path -LiteralPath $Target)) 'Rejected source should leave no destination map.'
    if (Test-Path -LiteralPath $Target) { Remove-Item -LiteralPath $Target -Recurse }
}

try {
    $null = New-Item -ItemType Directory -Path $Source, $Game -Force
    foreach ($Name in 'level', 'level.ltx', 'level.geom', 'level.cform', 'level.ai', 'level.spawn', 'level.cform1', 'lmap#1_1.dds', 'build.prj') {
        [IO.File]::WriteAllBytes((Join-Path $Source $Name), [byte[]] (1, 2, 3))
    }
    $SpawnPath = Join-Path $Source 'alife.spawn'
    Set-RespawnPoints 0 2 12
    $GridPath = Join-Path $Source 'level.ai'
    Remove-Item -LiteralPath $GridPath
    Assert-Rejected 'level.ai'
    [IO.File]::WriteAllBytes($GridPath, [byte[]] (1, 2, 3))

    Set-RespawnPoints 1 2 12
    Assert-Rejected 'two distinct team-0'
    Set-RespawnPoints 0 4 12
    Assert-Rejected 'two distinct team-0'
    Set-RespawnPoints 0 2 0
    Assert-Rejected 'two distinct team-0'

    Set-RespawnPoints 0 2 12 2
    Assert-Rejected 'numbered consecutively'

    Set-RespawnPoints 0 2 12
    [IO.File]::WriteAllBytes($SpawnPath, [byte[]] (5))
    $null = & $Prepare -EditorRoot $Editor -GameDataRoot $Game
    Assert-Result (!(Test-Path -LiteralPath (Join-Path $Target 'alife.spawn'))) 'An old ALife package stays in the editor folder.'
    Assert-Result (Test-Path -LiteralPath (Join-Path $Target 'level.cform1')) 'Split collision data belongs in the staged map.'
    Assert-Result (Test-Path -LiteralPath (Join-Path $Target 'lmap#1_1.dds')) 'The compiler lightmaps belong in the staged map.'
    Assert-Result (!(Test-Path -LiteralPath (Join-Path $Target 'level.spawn'))) 'Compiler actor and graph anchors must stay in the SDK.'
    Assert-Result (!(Test-Path -LiteralPath (Join-Path $Target 'build.prj'))) 'Compiler inputs must stay in the SDK.'
    Assert-Result (Test-Path -LiteralPath (Join-Path $Source 'level.spawn')) 'Staging preserves the editor output.'
    $ReportPath = Join-Path $Target 'w1-fixture.json'
    Assert-Result (Test-Path -LiteralPath $ReportPath) 'The fixture contains a hash report.'
    if (Test-Path -LiteralPath $ReportPath) {
        $Report = Get-Content -Raw -LiteralPath $ReportPath | ConvertFrom-Json
        $File = @($Report.files | Where-Object name -CEQ 'level.ai')
        Assert-Result ($Report.map -ceq 'w1_arena' -and $Report.deathmatchSpawnCount -eq 2 -and $File.Count -eq 1 -and
            $File[0].sha256 -ceq (Get-FileHash -LiteralPath $GridPath -Algorithm SHA256).Hash) 'Report identifies the map, spawns and staged file bytes.'
    }

    $ErrorText = ''
    try { $null = & $Prepare -EditorRoot $Editor -GameDataRoot $Game }
    catch { $ErrorText = $_.Exception.Message }
    Assert-Result ($ErrorText -like '*already exists*') 'Preparing again refuses to replace a staged map.'
}
finally {
    if (Test-Path -LiteralPath $Temp) { Remove-Item -LiteralPath $Temp -Recurse }
}
if ($Failures.Count) {
    foreach ($Failure in $Failures) { [Console]::Error.WriteLine($Failure) }
    exit 1
}
Write-Output 'W1 fixture preparation checks passed.'
