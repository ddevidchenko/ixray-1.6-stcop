#Requires -Version 7.4

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $W1Report,
    [Parameter(Mandatory)]
    [string] $Observations,
    [string] $FreshRecording,
    [string] $ReloadRecording
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'W0Evidence.ps1')
. (Join-Path $PSScriptRoot 'MP108Evidence.ps1')

function Copy-MP108Recording {
    param([string] $Path, [string] $Name, [string] $Bundle)

    if (!$Path) { return }
    $File = Get-Item -LiteralPath $Path
    if ($File -isnot [IO.FileInfo] -or !$File.Length -or $File.Extension -notin '.mp4', '.mkv', '.mov', '.webm') {
        throw 'Supply a nonempty recording with an mp4, mkv, mov, or webm extension.'
    }
    $Name += $File.Extension.ToLowerInvariant()
    $Target = Join-Path $Bundle $Name
    Copy-Item -LiteralPath $File.FullName -Destination $Target
    @{ path = $Name; sha256 = (Get-FileHash -LiteralPath $Target).Hash }
}

$W1File = Get-Item -LiteralPath $W1Report
$RunId = (Get-Date -AsUTC -Format 'yyyyMMddTHHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$Bundle = (New-Item -ItemType Directory -Path (Join-Path $W1File.DirectoryName "mp108-$RunId")).FullName
$ReportPath = Join-Path $Bundle 'report.json'
$Report = [ordered] @{ workItem = 'MP-108'; status = 'invalid_inputs'; error = $null }
try {
    Copy-Item -LiteralPath $W1File.FullName -Destination (Join-Path $Bundle 'w1-report.json')
    $W1 = Get-Content -LiteralPath (Join-Path $Bundle 'w1-report.json') -Raw | ConvertFrom-Json
    if ($W1.status -cne 'w1_navigation_passed' -or $W1.configuration -cne 'RelWithDebInfo' -or
        $W1.runtime.case -cne 'StalkerNavigation' -or $W1.runtime.map -cnotmatch '^[a-z0-9_-]{1,63}$') {
        throw 'Use the successful RelWithDebInfo StalkerNavigation report from this arena.'
    }
    $Runtime = Join-Path $W1.workspaceDirectory 'runtime'
    $Map = $W1.runtime.map
    $MapPrefix = "gamedata/levels/$Map"
    $Inputs = @{}
    foreach ($InputFile in $W1.runtime.inputFiles) {
        $RecordedPath = $InputFile.path.Replace('\', '/')
        $ActualHash = (Get-FileHash -LiteralPath (Join-Path $Runtime $RecordedPath)).Hash
        if ($ActualHash -cne $InputFile.sha256) {
            throw "Runtime input changed since the W1 startup run: $RecordedPath."
        }
        $Inputs[$RecordedPath] = $ActualHash
    }
    foreach ($Name in 'level.ai', 'level.navlinks', 'level.spawn', 'w1-fixture.json') {
        if (!$Inputs.ContainsKey("$MapPrefix/$Name")) { throw "The W1 report must include $Name from the route fixture." }
    }

    $Grid = [IO.File]::OpenRead((Join-Path $Runtime "$MapPrefix/level.ai"))
    try {
        $Header = [byte[]]::new(56)
        $Grid.ReadExactly($Header)
    } finally {
        $Grid.Dispose()
    }
    $Report.actualRevision = $W1.actualRevision
    $Report.configuration = $W1.configuration
    $Report.map = $Map
    $Report.gridGuid = '{0:x16} {1:x16}' -f [BitConverter]::ToUInt64($Header, 40), [BitConverter]::ToUInt64($Header, 48)
    $Report.agentProfile = 'stalker-v1'
    $Report.inputFiles = $Inputs
    Copy-Item -LiteralPath (Join-Path $Runtime "$MapPrefix/w1-fixture.json") -Destination $Bundle

    $ServerLogs = @(Get-ChildItem -LiteralPath (Join-Path $Runtime 'mp108-server/logs') -File -Filter '*.log')
    if ($ServerLogs.Count -ne 1) { throw 'Use an mp108-server overlay with one dedicated log covering both generations.' }
    $ServerLog = Join-Path $Bundle 'server.log'
    Copy-Item -LiteralPath $ServerLogs[0].FullName -Destination $ServerLog
    $Report.serverLog = @{ path = 'server.log'; sha256 = (Get-FileHash -LiteralPath $ServerLog).Hash }
    $Route = Get-MP108RouteRun (Get-Content -LiteralPath $ServerLog -Raw) $Map
    $Report.sameServerReload = $Route.sameServerReload

    $ObservationCopy = Join-Path $Bundle 'observations.json'
    Copy-Item -LiteralPath $Observations -Destination $ObservationCopy
    $Notes = Get-Content -LiteralPath $ObservationCopy -Raw | ConvertFrom-Json -AsHashtable
    $First = Copy-MP108Recording $FreshRecording 'fresh' $Bundle
    $Second = Copy-MP108Recording $ReloadRecording 'reloaded' $Bundle
    $ValidPair = $First -and $Second -and $First.sha256 -cne $Second.sha256
    $Report.fresh = @{
        native = $Route.fresh; recording = $First
        status = Get-MP108RouteObservationOutcome ($Notes['fresh'] ?? @{}) $Route.fresh.status ([bool] $First)
    }
    $Report.reloaded = @{
        native = $Route.reloaded; recording = $Second
        status = Get-MP108RouteObservationOutcome ($Notes['reloaded'] ?? @{}) $Route.reloaded.status ([bool] $Second)
    }
    $Report.status = 'incomplete'
    if ($Report.fresh.status -ceq 'failed' -or $Report.reloaded.status -ceq 'failed') {
        $Report.status = 'failed'
    } elseif ($Report.fresh.status -ceq 'observed' -and $Report.reloaded.status -ceq 'observed' -and
        $Report.sameServerReload -and $ValidPair -and ![string]::IsNullOrWhiteSpace($Notes['fixture'])) {
        $Report.status = 'observed'
    }
} catch {
    $Report.error = $_.Exception.Message
} finally {
    $Report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding utf8
    Write-Output $ReportPath
}
if ($Report.status -cne 'observed') { exit 1 }
exit 0
