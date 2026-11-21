#Requires -Version 7.4
param([Parameter(Mandatory)][string] $NavigationPacker)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Prepare = Join-Path $PSScriptRoot '../../../../scripts/multiplayer/windows/PrepareW1Fixture.ps1'
$Temp = Join-Path ([IO.Path]::GetTempPath()) ('w1-fixture-' + [guid]::NewGuid().ToString('N'))
$Editor = Join-Path $Temp 'editor'
$Game = Join-Path $Temp 'game'
$Source = Join-Path $Editor 'gamedata/levels/w1_arena'
$Target = Join-Path $Game 'gamedata/levels/w1_arena'
$Failures = [System.Collections.Generic.List[string]]::new()
$Materials = Join-Path $Game 'gamedata/gamemtl.xr'
$LinkSource = Join-Path $Temp 'ladder-source.txt'
$Inputs = @{ EditorRoot = $Editor; GameDataRoot = $Game; NavigationPacker = $NavigationPacker }

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
    try { $null = & $Prepare @Inputs }
    catch { $ErrorText = $_.Exception.Message }
    Assert-Result ($ErrorText -like "*$Text*") "Expected an error containing '$Text', received '$ErrorText'."
    Assert-Result (!(Test-Path -LiteralPath $Target)) 'Rejected source should leave no destination map.'
    if (Test-Path -LiteralPath $Target) { Remove-Item -LiteralPath $Target -Recurse }
    $Parent = Split-Path -Parent $Target
    if (Test-Path -LiteralPath $Parent) {
        Assert-Result (@(Get-ChildItem -LiteralPath $Parent -Filter '.w1-*' -Force).Count -eq 0) 'Failed packaging must clean the temporary directory.'
    }
}

try {
    $null = New-Item -ItemType Directory -Path $Source, (Split-Path -Parent $Materials) -Force
    foreach ($Name in 'level', 'level.ltx', 'level.geom', 'level.cform', 'level.ai', 'level.spawn', 'level_0.cform', 'level_1.cform', 'lmap#1_1.dds', 'build.prj') {
        [IO.File]::WriteAllBytes((Join-Path $Source $Name), [byte[]] (1, 2, 3))
    }
    $SpawnPath = Join-Path $Source 'alife.spawn'
    Set-RespawnPoints 0 2 12
    $GridPath = Join-Path $Source 'level.ai'
    Remove-Item -LiteralPath $GridPath
    Assert-Rejected 'level.ai'
    [IO.File]::WriteAllBytes($GridPath, [byte[]] (1, 2, 3))
    [IO.File]::WriteAllBytes($Materials, [byte[]] (7, 8, 9))
    Assert-Rejected 'level.ai'

    $Grid = [byte[]]::new(56 + 24 * 2)
    $Grid[0] = 11
    $Grid[4] = 2
    [BitConverter]::GetBytes([single] 1).CopyTo($Grid, 8)
    [BitConverter]::GetBytes([single] 1).CopyTo($Grid, 12)
    $Grid[40] = 0x42
    $Grid[48] = 0xAB
    [IO.File]::WriteAllBytes($GridPath, $Grid)

    $Collision = [byte[]]::new(40)
    $Collision[0] = 5
    $Collision[36] = 2
    [IO.File]::WriteAllBytes((Join-Path $Source 'level.cform'), $Collision)
    $Part = Join-Path $Source 'level_1.cform'
    Remove-Item -LiteralPath $Part
    Assert-Rejected 'level_1.cform'
    [IO.File]::WriteAllBytes($Part, [byte[]] (21, 22, 23))
    Remove-Item -LiteralPath $Materials
    Assert-Rejected 'gamemtl.xr'
    [IO.File]::WriteAllBytes($Materials, [byte[]] (7, 8, 9))

    $PartialPacker = Join-Path $Temp 'partial-packer.ps1'
    @'
[IO.File]::WriteAllText((Join-Path $args[0] 'level.navlinks'), 'partial output')
exit 7
'@ | Set-Content -LiteralPath $PartialPacker -Encoding ascii
    $Inputs.NavigationPacker = $PartialPacker
    Assert-Rejected 'Navigation packaging failed'
    $Inputs.NavigationPacker = $NavigationPacker

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
    $null = & $Prepare @Inputs
    Assert-Result (!(Test-Path -LiteralPath (Join-Path $Target 'alife.spawn'))) 'An old ALife package stays in the editor folder.'
    Assert-Result (Test-Path -LiteralPath (Join-Path $Target 'level_1.cform')) 'Split collision data belongs in the staged map.'
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
    try { $null = & $Prepare @Inputs }
    catch { $ErrorText = $_.Exception.Message }
    Assert-Result ($ErrorText -like '*already exists*') 'Preparing again refuses to replace a staged map.'
    $PackagePath = Join-Path $Target 'level.navlinks'
    Assert-Result (Test-Path -LiteralPath $PackagePath) 'A walk-only fixture still has a navigation package.'
    if (Test-Path -LiteralPath $PackagePath) {
        $Package = Get-Content -Raw -LiteralPath $PackagePath
        Assert-Result ($Package.Contains("level.spawn -`n")) 'The walk-only package records that level.spawn was deliberately omitted.'
        Assert-Result ($Package.Contains("parts 2`n")) 'The package binds the exact native collision part count.'
        Assert-Result ($Package.Contains((Get-FileHash -LiteralPath $Materials -Algorithm SHA256).Hash.ToLowerInvariant())) 'Staging binds the material file.'
        $Before = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash
        try { $null = & $Prepare @Inputs } catch {}
        Assert-Result ((Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash -ceq $Before) 'A failed replacement leaves the previous package intact.'
    }

    Remove-Item -LiteralPath $Target -Recurse
    $Inputs.LadderSource = $LinkSource
    Set-Content -LiteralPath $LinkSource -Encoding ascii -Value 'arena_ladders 1 43 ab', 'clmbl#0 up 0 1 2'
    Assert-Rejected 'GUID'
    Set-Content -LiteralPath $LinkSource -Encoding ascii -Value 'arena_ladders 1 42 ab', 'clmbl#0 up 0 2 2'
    Assert-Rejected 'traversal'
    Set-Content -LiteralPath $LinkSource -Encoding ascii -Value 'arena_ladders 1 42 ab', 'clmbl#0 up 0 1 2'
    $null = & $Prepare @Inputs
    Assert-Result (Test-Path -LiteralPath (Join-Path $Target 'level.spawn')) 'A ladder fixture publishes the cooked spawn bytes.'
    $Report = Get-Content -Raw -LiteralPath (Join-Path $Target 'w1-fixture.json') | ConvertFrom-Json
    Assert-Result ($Report.mapVersion -ceq '1.0' -and @($Report.files | Where-Object name -ceq 'level.navlinks').Count -eq 1) 'The deployment manifest records the version and package digest.'
    Assert-Result ($Report.materials.sha256 -ceq (Get-FileHash -LiteralPath $Materials -Algorithm SHA256).Hash) 'The deployment manifest records the material digest.'

    Remove-Item -LiteralPath $Target -Recurse
    $Inputs.AutoLadders = $true
    Assert-Rejected 'Choose either'
    $Inputs.Remove('LadderSource')
    Assert-Rejected 'level.spawn'

    # One editor-style ladder named clmbl#0, with a planar box spanning Y=0 to Y=4.
    $Packet = [Convert]::FromHexString('0100636c696d61626c655f6f626a65637400636c6d626c23300000000000000000000040000000000000000000000000000000000000ffffffffffff20008700020000000000ffff6000000000000000000000000000000000000000000000000000000000010100000000000000000000000000000000000000400000000000000000000000006666e63e0000000000000000000000006d6174657269616c735c6c616464657200')
    $CookedSpawn = Join-Path $Source 'level.spawn'
    [IO.File]::WriteAllBytes($CookedSpawn, (ConvertTo-Chunk 0 $Packet))

    # Four nodes in two XZ columns. The lower pair and upper pair each share one walk edge in both directions.
    $AutoGrid = [byte[]]::new(56 + 24 * 4)
    $Grid[0..55].CopyTo($AutoGrid, 0)
    $AutoGrid[4] = 4
    [BitConverter]::GetBytes([single] 4).CopyTo($AutoGrid, 12)
    [BitConverter]::GetBytes([single] 3).CopyTo($AutoGrid, 28)
    [BitConverter]::GetBytes([single] 4).CopyTo($AutoGrid, 32)
    for ($Index = 0; $Index -lt 4; ++$Index) {
        $Offset = 56 + 24 * $Index
        [byte[]]::new(13).CopyTo($AutoGrid, $Offset)
        [array]::Fill($AutoGrid, [byte] 255, $Offset + 3, 10)
        $AutoGrid[$Offset + 3] = 254
        $AutoGrid[$Offset] = ($Index + 2) % 4
        $AutoGrid[$Offset + 19] = if ($Index -lt 2) { 1 } else { 2 }
        if ($Index % 2) { [BitConverter]::GetBytes([uint16] 65535).CopyTo($AutoGrid, $Offset + 22) }
    }
    [IO.File]::WriteAllBytes($GridPath, $AutoGrid)

    # Vanilla collision contains a floor triangle beneath each landing.
    $Stream = [IO.MemoryStream]::new()
    $Writer = [IO.BinaryWriter]::new($Stream)
    try {
        $Writer.Write([uint32] 4)
        $Writer.Write([uint32] 6)
        $Writer.Write([uint32] 2)
        $Writer.Write([byte[]]::new(24))
        foreach ($Coordinate in -2,0,-3, -2,0,3, 5,0,0, -2,4,-3, -2,4,3, 5,4,0) { $Writer.Write([single] $Coordinate) }
        foreach ($Index in 0,1,2,0, 3,4,5,0) { $Writer.Write([uint32] $Index) }
        [IO.File]::WriteAllBytes((Join-Path $Source 'level.cform'), $Stream.ToArray())
    }
    finally { $Writer.Dispose() }

    $null = & $Prepare @Inputs
    $AutoPackage = Get-Content -Raw -LiteralPath (Join-Path $Target 'level.navlinks')
    Assert-Result ($AutoPackage -match '(?m)^clmbl#0 up 0 1 [0-9.]+' -and $AutoPackage -match '(?m)^clmbl#0 down 1 0 [0-9.]+') 'Automatic staging publishes both validated ladder directions.'
    Assert-Result ($AutoPackage.Contains((Get-FileHash -LiteralPath $CookedSpawn).Hash.ToLowerInvariant())) 'Automatic links bind the exact spawn bytes that supplied their semantic geometry.'
    Assert-Result (Select-String -Quiet -LiteralPath (Join-Path $Target 'w1-ladders.txt') -Pattern 'clmbl#0.*validated') 'The staged fixture keeps the automatic ladder diagnostics.'

    Remove-Item -LiteralPath $Target -Recurse
    $Packet[64] = 1 # This fixture's game type mask now enables single-player only.
    [IO.File]::WriteAllBytes($CookedSpawn, (ConvertTo-Chunk 0 $Packet))
    $null = & $Prepare @Inputs
    $DisabledPackage = Get-Content -Raw -LiteralPath (Join-Path $Target 'level.navlinks')
    Assert-Result ($DisabledPackage -cnotmatch '(?m)^clmbl#0 ' -and
        (Select-String -Quiet -LiteralPath (Join-Path $Target 'w1-ladders.txt') -Pattern 'disabled in deathmatch')) 'A mode-disabled ladder is diagnosed and omitted from the navigation package.'
}
finally {
    if (Test-Path -LiteralPath $Temp) { Remove-Item -LiteralPath $Temp -Force -Recurse }
}
if ($Failures.Count) {
    foreach ($Failure in $Failures) { [Console]::Error.WriteLine($Failure) }
    exit 1
}
Write-Output 'W1 fixture preparation checks passed.'
