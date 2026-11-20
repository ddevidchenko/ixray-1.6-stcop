#Requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $EditorRoot,
    [Parameter(Mandatory)][string] $GameDataRoot,
    [ValidatePattern('^[a-z0-9_-]{1,63}$')][string] $Map = 'w1_arena'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-W1Chunks {
    param([IO.BinaryReader] $Reader, [long] $End, [string] $Context)

    $Stream = $Reader.BaseStream
    while ($Stream.Position -lt $End) {
        if ($End - $Stream.Position -lt 8) { throw "Truncated chunk header in $Context." }
        $Header = $Reader.ReadUInt32()
        $Size = $Reader.ReadUInt32()
        $Offset = $Stream.Position
        if ($Size -gt $End - $Offset) { throw "Truncated chunk body in $Context." }
        [pscustomobject] @{
            Id = $Header -band 0x7fffffff
            Compressed = ($Header -band 0x80000000u) -ne 0
            Offset = $Offset
            Size = $Size
        }
        $Stream.Position = $Offset + $Size
    }
}

function Find-W1Chunk {
    param([object[]] $Chunks, [uint32] $Id, [string] $Name)

    $MatchingChunks = @($Chunks | Where-Object Id -EQ $Id)
    if ($MatchingChunks.Count -ne 1 -or $MatchingChunks[0].Size -eq 0 -or $MatchingChunks[0].Compressed) {
        throw "$Name needs one nonempty, uncompressed chunk $Id."
    }
    return $MatchingChunks[0]
}

function Get-W1RespawnCount {
    param([string] $Path)

    $Reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    try {
        $Chunks = @(Read-W1Chunks $Reader $Reader.BaseStream.Length 'level.game')
        # POINT_BASE + ptRPoint from xrServerEntities/LevelGameDef.h.
        $Points = Find-W1Chunk $Chunks 0x2000 'level.game (respawn points)'
        $Reader.BaseStream.Position = $Points.Offset
        $Records = @(Read-W1Chunks $Reader ($Points.Offset + $Points.Size) 'level.game respawn points')
        $Positions = [System.Collections.Generic.HashSet[System.ValueTuple[single,single,single]]]::new()
        for ($Index = 0; $Index -lt $Records.Count; ++$Index) {
            $Record = $Records[$Index]
            if ($Record.Id -ne $Index) { throw 'level.game respawn points must be numbered consecutively from zero.' }
            if ($Record.Compressed -or $Record.Size -lt 28) { throw 'Truncated or compressed respawn point in level.game.' }
            $Reader.BaseStream.Position = $Record.Offset
            $X = $Reader.ReadSingle()
            $Y = $Reader.ReadSingle()
            $Z = $Reader.ReadSingle()
            $Reader.BaseStream.Position = $Record.Offset + 24
            $Team = $Reader.ReadByte()
            $Kind = $Reader.ReadByte()
            $Mode = $Reader.ReadUInt16()
            if ($Team -eq 0 -and $Kind -eq 0 -and ($Mode -band 2) -ne 0 -and
                [single]::IsFinite($X) -and [single]::IsFinite($Y) -and [single]::IsFinite($Z)) {
                $null = $Positions.Add([System.ValueTuple[single,single,single]]::new($X, $Y, $Z))
            }
        }
        if ($Positions.Count -lt 2) { throw 'W1 needs two distinct team-0 Actor Spawn points with deathmatch enabled.' }
        return $Positions.Count
    }
    finally { $Reader.Dispose() }
}

$Source = Join-Path (Resolve-Path -LiteralPath $EditorRoot).Path "gamedata/levels/$Map"
$Parent = Join-Path (Resolve-Path -LiteralPath $GameDataRoot).Path 'gamedata/levels'
$Target = Join-Path $Parent $Map
if (Test-Path -LiteralPath $Target) { throw "The staged map already exists at $Target. Use a fresh runtime copy for a new bake." }

foreach ($Name in 'level', 'level.ltx', 'level.geom', 'level.cform', 'level.game', 'level.ai') {
    $File = Get-Item -LiteralPath (Join-Path $Source $Name) -ErrorAction SilentlyContinue
    if ($File -isnot [IO.FileInfo] -or $File.Length -eq 0) { throw "Missing or empty $Name in $Source. Finish the editor/compiler steps first." }
}
$SpawnCount = Get-W1RespawnCount (Join-Path $Source 'level.game')

$null = New-Item -ItemType Directory -Path $Parent -Force
$Pending = Join-Path $Parent ('.w1-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $Pending
try {
    $Files = @(Get-ChildItem -LiteralPath $Source -File | Where-Object {
        $_.Name -eq 'level' -or $_.Name -like 'lmap#*.dds' -or
        ($_.Name -like 'level.*' -and $_.Name -ne 'level.spawn')
    } | Sort-Object Name | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Pending
        $Hash = (Get-FileHash -LiteralPath (Join-Path $Pending $_.Name) -Algorithm SHA256).Hash
        [ordered] @{ name = $_.Name; sha256 = $Hash }
    })
    [ordered] @{ map = $Map; deathmatchSpawnCount = $SpawnCount; files = $Files } |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Pending 'w1-fixture.json') -Encoding utf8
    [IO.Directory]::Move($Pending, $Target)
}
finally {
    if (Test-Path -LiteralPath $Pending) { Remove-Item -LiteralPath $Pending -Recurse }
}
Write-Output (Join-Path $Target 'w1-fixture.json')
