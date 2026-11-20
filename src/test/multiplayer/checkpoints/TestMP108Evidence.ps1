#Requires -Version 7.4

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../../../scripts/multiplayer/windows/W0Evidence.ps1')
. (Join-Path $PSScriptRoot '../../../../scripts/multiplayer/windows/MP108Evidence.ps1')

$Failures = [System.Collections.Generic.List[string]]::new()
function Assert-Result {
    param([bool] $Condition, [string] $Description)
    if (!$Condition) { $Failures.Add($Description) }
}

$RouteReady = 'Arena navigation generation: map=mp_atp ready=10'
$RouteWalk = 'MP108 walk: generation=10 stalker=42 method=checked'
$RouteBlocked = @(
    'Arena ladder: generation=10 stalker=42 link=2 112->230 mount'
    'Arena ladder: generation=10 stalker=42 link=2 climb'
    'Arena ladder: generation=10 stalker=42 link=2 dismount'
    'Arena ladder: generation=10 stalker=42 link=2 failed'
)
$RouteUp = @(
    'Arena ladder: generation=10 stalker=42 link=2 112->230 mount'
    'Arena ladder: generation=10 stalker=42 link=2 climb'
    'Arena ladder: generation=10 stalker=42 link=2 dismount'
    'Arena ladder: generation=10 stalker=42 link=2 grounded exit'
)
$RouteDown = @(
    'Arena ladder: generation=10 stalker=42 link=3 230->112 mount'
    'Arena ladder: generation=10 stalker=42 link=3 climb'
    'Arena ladder: generation=10 stalker=42 link=3 dismount'
    'Arena ladder: generation=10 stalker=42 link=3 grounded exit'
)
$RouteLines = @($RouteReady, $RouteWalk) + $RouteBlocked + $RouteUp + @($RouteWalk) + $RouteDown + @($RouteWalk)
$RouteOutput = $RouteLines -join "`n"
$Route = Get-MP108RouteEvidence $RouteOutput 'mp_atp'
Assert-Result ($Route.status -ceq 'observed' -and $Route.generation -eq 10 -and $Route.stalker -eq 42) 'One authoritative stalker completes both directions, walks after each climb, and recovers after a blocked exit.'
Assert-Result ((Get-MP108RouteEvidence (($RouteLines + 'MP108 walk: generation=10 stalker=42 method=direct') -join "`n") 'mp_atp').status -ceq 'direct_placement') 'Direct placement fails the physical route checkpoint even if all ladder markers appear.'
Assert-Result ((Get-MP108RouteEvidence (($RouteLines[0..($RouteLines.Length - 2)]) -join "`n") 'mp_atp').status -ceq 'incomplete') 'A grounded exit without subsequent walking cannot pass.'
Assert-Result ((Get-MP108RouteEvidence ($RouteOutput.Replace('stalker=42 link=3 ', 'stalker=55 link=3 ')) 'mp_atp').status -ceq 'incomplete') 'Two bodies cannot provide the two directions of one route proof.'
Assert-Result ((Get-MP108RouteEvidence ($RouteOutput.Replace('230->112 mount', '231->113 mount')) 'mp_atp').status -ceq 'incomplete') 'Successful climbs on unrelated node pairs do not prove both directions.'
Assert-Result ((Get-MP108RouteEvidence ($RouteOutput.Replace("Arena ladder: generation=10 stalker=42 link=3 climb`n", '')) 'mp_atp').status -ceq 'incomplete') 'A skipped motor phase is incomplete even when the route eventually reports an exit.'
Assert-Result ((Get-MP108RouteEvidence ((@($RouteReady, $RouteWalk) + $RouteUp + @($RouteWalk) + $RouteDown + @($RouteWalk)) -join "`n") 'mp_atp').status -ceq 'incomplete') 'Two clear climbs alone cannot show blocked-exit recovery.'
$Reloaded = @($RouteLines + 'Arena navigation generation: map=mp_atp closing=10' + 'Arena navigation generation: map=mp_atp ready=11' + $RouteWalk)
Assert-Result ((Get-MP108RouteEvidence ($Reloaded -join "`n") 'mp_atp').status -ceq 'incomplete') 'A fresh generation cannot borrow route evidence from an old map even when the map and node IDs match.'
Assert-Result ((Get-MP108RouteEvidence $RouteOutput 'different_map').status -ceq 'incomplete') 'An unrelated map cannot pass from matching entity IDs.'
$WrongStalkerWalk = @($RouteReady, ($RouteWalk.Replace('stalker=42', 'stalker=55'))) + $RouteBlocked + $RouteUp + @($RouteWalk) + $RouteDown + @($RouteWalk)
Assert-Result ((Get-MP108RouteEvidence ($WrongStalkerWalk -join "`n") 'mp_atp').status -ceq 'incomplete') 'Recovery must start with walking from the stalker that actually uses the ladder.'

$SinglePostClimbStep = (@($RouteReady, $RouteWalk) + $RouteBlocked + $RouteUp + $RouteDown + @($RouteWalk)) -join "`n"
Assert-Result ((Get-MP108RouteEvidence $SinglePostClimbStep 'mp_atp').status -ceq 'incomplete') 'One later walking sample cannot prove both dismounts resumed walking.'
$BeforeReload = (@($RouteReady, $RouteWalk) + $RouteUp + @($RouteWalk, 'Arena navigation generation: map=mp_atp closing=10', 'Arena navigation generation: map=mp_atp ready=11')) -join "`n"
$StaleContinuation = ($RouteBlocked + $RouteUp + @($RouteWalk) + $RouteDown + @($RouteWalk)) -join "`n"
Assert-Result ((Get-MP108RouteEvidence "$BeforeReload`n$StaleContinuation" 'mp_atp').status -ceq 'incomplete') 'Stale walk records cannot seed a new generation.'
$ServerReady = 'DM ready: role=server peer=1 pid=21'
$ReadyOutput = $RouteOutput.Replace($RouteReady, "$RouteReady`n$ServerReady")
$ReloadOutput = $ReadyOutput.Replace('ready=10', 'ready=11').Replace('generation=10 ', 'generation=11 ')
$Close = 'Arena navigation generation: map=mp_atp closing=10'
$TwoRuns = "$ReadyOutput`n$Close`n$ReloadOutput"
$Run = Get-MP108RouteRun $TwoRuns 'mp_atp'
Assert-Result ($Run.fresh.generation -eq 10 -and $Run.reloaded.generation -eq 11 -and $Run.sameServerReload) 'A higher generation after closing the same map on the same process qualifies as an in-process reload.'
Assert-Result ($Run.fresh.status -ceq 'observed' -and $Run.reloaded.status -ceq 'observed') 'Each generation must supply its own complete ladder sequence.'
Assert-Result (!(Get-MP108RouteRun "$ReadyOutput`n$ReloadOutput" 'mp_atp').sameServerReload) 'A second ready label alone cannot prove ordered teardown and reload.'
$LateDmReady = "$RouteOutput`n$Close`n$ServerReady`n$ReloadOutput"
Assert-Result (!(Get-MP108RouteRun $LateDmReady 'mp_atp').sameServerReload) 'Initial DM readiness must precede closing its arena.'
Assert-Result (!(Get-MP108RouteRun ($TwoRuns.Replace("ready=11`n$ServerReady", "ready=11`nDM ready: role=server peer=2 pid=22")) 'mp_atp').sameServerReload) 'Two separate server processes cannot prove in-process reload.'
Assert-Result (!(Get-MP108RouteRun ($TwoRuns.Replace("ready=11`n$ServerReady", "ready=11")) 'mp_atp').sameServerReload) 'The reloaded arena must also reach DM readiness.'
Assert-Result (!(Get-MP108RouteRun "$ReadyOutput`n$Close`nArena navigation generation: map=other ready=11`n$ReloadOutput" 'mp_atp').sameServerReload) 'A different intervening arena cannot be reported as the controlled same-map reload.'
$StalePhases = $TwoRuns.Replace('Arena ladder: generation=11 ', 'Arena ladder: generation=10 ')
Assert-Result ((Get-MP108RouteRun $StalePhases 'mp_atp').reloaded.status -ceq 'incomplete') 'Even with new walking samples, stale ladder phases cannot fill the reload checklist.'
$DirectWalk = 'MP108 walk: generation=10 stalker=42 method=direct'
$DirectFirst = Get-MP108RouteRun "$ReadyOutput`n$DirectWalk`n$Close`n$ReloadOutput`n$DirectWalk" 'mp_atp'
Assert-Result ($DirectFirst.fresh.status -ceq 'direct_placement' -and $DirectFirst.reloaded.status -ceq 'observed') 'A direct placement fails only the generation that produced it.'
$WrongId = $RouteOutput.Replace('stalker=42', 'stalker=4294967296')
Assert-Result ((Get-MP108RouteEvidence $WrongId 'mp_atp').status -ceq 'incomplete') 'A malformed actor ID cannot pass or throw during report creation.'

$AllChecks = [ordered] @{}
foreach ($Check in (Get-MP108RouteChecks)) { $AllChecks[$Check] = 'pass' }
$MissingCheck = [ordered] @{ stairs = 'pass' }
$FailedCheck = [ordered] @{ stairs = 'fail' }
Assert-Result ((Get-MP108RouteObservationOutcome $AllChecks 'observed' $false) -ceq 'incomplete') 'The visual gate requires a client recording.'
Assert-Result ((Get-MP108RouteObservationOutcome $MissingCheck 'observed' $true) -ceq 'incomplete') 'An unfinished visual checklist cannot pass.'
Assert-Result ((Get-MP108RouteObservationOutcome $AllChecks 'incomplete' $true) -ceq 'incomplete') 'Operator confirmations without native route evidence remain incomplete.'
Assert-Result ((Get-MP108RouteObservationOutcome $MissingCheck 'direct_placement' $false) -ceq 'failed') 'A recorded placement shortcut remains a failure when the operator stops early.'
Assert-Result ((Get-MP108RouteObservationOutcome $FailedCheck 'observed' $true) -ceq 'failed') 'Native logs cannot override a failed visual observation.'
Assert-Result ((Get-MP108RouteObservationOutcome $AllChecks 'observed' $true) -ceq 'observed') 'Each generation requires both visual and native evidence.'

$Collector = Join-Path $PSScriptRoot '../../../../scripts/multiplayer/windows/CollectMP108Routes.ps1'
$Scratch = (New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) ('mp108-' + [guid]::NewGuid()))).FullName
try {
    $Runtime = Join-Path $Scratch 'runtime'
    $Level = (New-Item -ItemType Directory -Path (Join-Path $Runtime 'gamedata/levels/mp_atp') -Force).FullName
    $LogDir = (New-Item -ItemType Directory -Path (Join-Path $Runtime 'mp108-server/logs') -Force).FullName
    Set-Content -LiteralPath (Join-Path $LogDir 'server.log') -Value $TwoRuns
    $Grid = [byte[]]::new(56)
    $Grid[40] = 12
    $Grid[48] = 34
    [IO.File]::WriteAllBytes((Join-Path $Level 'level.ai'), $Grid)
    'level.navlinks', 'level.spawn', 'w1-fixture.json' | ForEach-Object {
        Set-Content -LiteralPath (Join-Path $Level $_) -Value $_
    }
    $W1 = Join-Path $Scratch 'w1.json'
    @{
        status = 'w1_navigation_passed'; actualRevision = 'test-revision'; configuration = 'RelWithDebInfo'
        workspaceDirectory = $Scratch
        runtime = @{
            map = 'mp_atp'; case = 'StalkerNavigation'
            inputFiles = @(Get-ChildItem -LiteralPath $Level -File | ForEach-Object {
                @{ path = "gamedata/levels/mp_atp/$($_.Name)"; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
            })
        }
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $W1
    $Notes = Join-Path $Scratch 'observations.json'
    @{ fixture = 'Synthetic collector fixture'; fresh = $AllChecks; reloaded = $AllChecks } |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Notes
    $FirstClip = Join-Path $Scratch 'first.mp4'
    $SecondClip = Join-Path $Scratch 'second.mp4'
    Set-Content -LiteralPath $FirstClip -Value 'fresh capture'
    Set-Content -LiteralPath $SecondClip -Value 'reload capture'
    $Options = @{ W1Report = $W1; Observations = $Notes; FreshRecording = $FirstClip; ReloadRecording = $SecondClip }

    $ResultPath = & $Collector @Options
    $Result = Get-Content -Raw -LiteralPath $ResultPath | ConvertFrom-Json
    Assert-Result ($LASTEXITCODE -eq 0 -and $Result.status -ceq 'observed' -and $Result.reloaded.native.generation -eq 11) 'The collector combines both verified observation windows.'
    Assert-Result ($Result.gridGuid -ceq '000000000000000c 0000000000000022' -and $Result.actualRevision -ceq 'test-revision') 'The report records the loaded grid ID and candidate commit.'
    $CopiedClip = Join-Path (Split-Path $ResultPath) $Result.fresh.recording.path
    Assert-Result ((Get-FileHash -LiteralPath $CopiedClip).Hash -ceq $Result.fresh.recording.sha256) 'The evidence bundle copies and hashes the original capture.'

    Set-Content -LiteralPath $SecondClip -Value 'fresh capture'
    $DuplicatePath = & $Collector @Options
    $Duplicate = Get-Content -Raw -LiteralPath $DuplicatePath | ConvertFrom-Json
    Assert-Result ($LASTEXITCODE -eq 1 -and $Duplicate.status -ceq 'incomplete') 'A copied clip cannot count as both observations.'

    Add-Content -LiteralPath (Join-Path $Level 'level.navlinks') -Value 'changed'
    $ChangedPath = & $Collector @Options
    $Changed = Get-Content -Raw -LiteralPath $ChangedPath | ConvertFrom-Json
    Assert-Result ($LASTEXITCODE -eq 1 -and $Changed.status -ceq 'invalid_inputs' -and $Changed.error -like '*level.navlinks*') 'A changed runtime package invalidates the evidence even when both capture windows have already passed.'
} finally {
    Remove-Item -LiteralPath $Scratch -Force -Recurse
}

if ($Failures.Count) { exit 1 }
Write-Host 'MP-108 evidence checks passed.'
