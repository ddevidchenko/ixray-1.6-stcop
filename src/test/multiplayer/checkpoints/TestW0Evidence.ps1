#Requires -Version 7.4

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../../../scripts/multiplayer/windows/W0Evidence.ps1')
. (Join-Path $PSScriptRoot '../../../../scripts/multiplayer/windows/W0Runtime.ps1')

$Failures = [System.Collections.Generic.List[string]]::new()
function Assert-Result {
    param([bool] $Condition, [string] $Description)
    if (!$Condition) { $Failures.Add($Description) }
}

$BindFailure = '[SteamNetServer] Failed to listen on port 5446'
$StartupFailure = 'Failed to start server.'
Assert-Result ((Get-W0ListenerOutcome $BindFailure 5446) -ceq 'pending') 'A low-level bind error needs a propagated startup failure.'
Assert-Result ((Get-W0ListenerOutcome "$BindFailure`n$StartupFailure" 5446) -ceq 'failed') 'An ordered bind and startup failure is the intended regression outcome.'
Assert-Result ((Get-W0ListenerOutcome "$StartupFailure`n$BindFailure" 5446) -ceq 'pending') 'A prior startup failure does not explain a later bind failure.'
Assert-Result ((Get-W0ListenerOutcome "${BindFailure}0`n$StartupFailure" 5446) -ceq 'pending') 'A port prefix is insufficient.'
Assert-Result ((Get-W0ListenerOutcome "$BindFailure`n$StartupFailure`nDM ready: role=server peer=1 pid=21" 5446) -ceq 'ready') 'A ready server contradicts the failed startup.'
$InternalJoin = '[SteamNetClient] Connecting to server at 127.0.0.1:5446'
Assert-Result ((Get-W0ListenerOutcome "$BindFailure`n$InternalJoin" 5446) -ceq 'continued') 'An internal client attempt after the bind error demonstrates continued startup.'
Assert-Result ((Get-W0ListenerOutcome "$InternalJoin`n$BindFailure" 5446) -ceq 'pending') 'Only an internal client attempt after the failed bind is evidence of continued startup.'

$NavigationReady = 'W1 navigation: status=ready map=mp_atp'
$NavigationFailed = 'W1 navigation: status=failed map=mp_atp reason=missing level.ai'
Assert-Result ((Get-W1NavigationOutcome '' 'mp_atp') -ceq 'pending') 'An empty log must wait for a W1 navigation record.'
Assert-Result ((Get-W1NavigationOutcome "prefix $NavigationReady" 'mp_atp') -ceq 'pending') 'Only an entire W1 navigation line is evidence.'
Assert-Result ((Get-W1NavigationOutcome $NavigationReady 'mp_other') -ceq 'pending') 'Readiness for a different map cannot pass.'
Assert-Result ((Get-W1NavigationOutcome "$NavigationReady`r`n$NavigationReady" 'mp_atp') -ceq 'ready') 'Repeated readiness for the same map is allowed.'
Assert-Result ((Get-W1NavigationOutcome $NavigationFailed 'mp_atp') -ceq 'failed') 'Explicit same-map fixture failure is terminal.'
Assert-Result ((Get-W1NavigationOutcome "$NavigationReady`n$NavigationFailed" 'mp_atp') -ceq 'failed') 'A failure overrides earlier readiness.'
Assert-Result ((Get-W1NavigationOutcome "$NavigationFailed`n$NavigationReady" 'mp_atp') -ceq 'failed') 'A contradictory ready record cannot hide a failure.'

Assert-Result ((Get-W1NavigationOutcome '' 'mp_atp' $true) -ceq 'not_loaded') 'DM readiness without prior navigation is a behavioral failure.'
Assert-Result ((Get-W1NavigationOutcome $NavigationReady 'mp_atp' $true) -ceq 'ready') 'Navigation stays ready when DM becomes ready.'
Assert-Result ((Get-W1NavigationOutcome "$NavigationReady extra" 'mp_atp' $true) -ceq 'not_loaded') 'A malformed readiness record cannot pass.'
Assert-Result ((Get-W1NavigationOutcome "$NavigationFailed`nW1 navigation: status=ready map=mp_other" 'mp_other') -ceq 'ready') 'Failures for other maps do not override matched evidence.'

$ListenerReady = '[SteamNetServer] created on port 5446'
$MissingGrid = 'Arena navigation: map=mp_atp file=level.ai reason=missing or invalid native grid'
$BadPackage = 'Arena navigation: map=mp_atp version=1.0 file=level.navlinks reason=traversal size or SHA-256 mismatch'
$MissingPackage = 'Arena navigation: map=mp_atp version=1.0 file=level.navlinks reason=navigation package missing'
$RejectedGrid = "$MissingGrid`n$NavigationFailed`n$StartupFailure"
$RejectedPackage = "$BadPackage`n$NavigationFailed`n$StartupFailure"
Assert-Result ((Get-W1NavigationOutcome "$ListenerReady`n$NavigationReady" 'mp_atp' $true $true) -ceq 'late') 'Readiness after opening the listener fails the dedicated bootstrap check.'
Assert-Result ((Get-W1NavigationOutcome "$NavigationReady`n$ListenerReady" 'mp_atp' $true $true) -ceq 'ready') 'Validated navigation precedes the listener.'
Assert-Result ((Get-W1NavigationOutcome $NavigationReady 'mp_atp' $true $true) -ceq 'not_listening') 'Dedicated readiness also requires evidence of the listener opening after validation.'
Assert-Result ((Get-W1RejectionOutcome $RejectedGrid 'mp_atp' 'MissingGrid') -ceq 'rejected') 'Missing grid rejection has matching map, input, and terminal startup failure.'
Assert-Result ((Get-W1RejectionOutcome $RejectedPackage 'mp_atp' 'CorruptPackage') -ceq 'rejected') 'Corrupt payload rejection identifies the actual package error.'
Assert-Result ((Get-W1RejectionOutcome "$MissingPackage`n$NavigationFailed`n$StartupFailure" 'mp_atp' 'MissingPackage') -ceq 'rejected') 'A missing package produces its own rejection reason.'
Assert-Result ((Get-W1RejectionOutcome $RejectedPackage 'mp_atp' 'MissingPackage') -ceq 'pending') 'A different navigation failure cannot pass an injected fault.'
Assert-Result ((Get-W1RejectionOutcome "$ListenerReady`n$RejectedGrid" 'mp_atp' 'MissingGrid') -ceq 'continued') 'The listener must never have opened on a bad navmesh.'
Assert-Result ((Get-W1RejectionOutcome "$RejectedGrid`n[SteamNetServer] Failed to open listener on port 5446 (1)" 'mp_atp' 'MissingGrid') -ceq 'continued') 'An attempted listener bind contradicts navigation rejection.'
Assert-Result ((Get-W1RejectionOutcome "$RejectedGrid`n$InternalJoin" 'mp_atp' 'MissingGrid') -ceq 'continued') 'An internal connect attempt contradicts rejection.'
Assert-Result ((Get-W1RejectionOutcome "$NavigationFailed`n$StartupFailure" 'mp_atp' 'MissingGrid') -ceq 'pending') 'Generic failure without matching asset evidence cannot pass.'
Assert-Result ((Get-W1RejectionOutcome "$StartupFailure`n$MissingGrid`n$NavigationFailed" 'mp_atp' 'MissingGrid') -ceq 'pending') 'A prior startup failure cannot explain later output.'

$FaultRoot = Join-Path ([IO.Path]::GetTempPath()) ('w1-fault-' + [guid]::NewGuid().ToString('N'))
try {
    $MapDir = (New-Item -ItemType Directory -Path (Join-Path $FaultRoot 'gamedata/levels/mp_atp') -Force).FullName
    $PackagePath = Join-Path $MapDir 'level.navlinks'
    $GridPath = Join-Path $MapDir 'level.ai'
    [IO.File]::WriteAllBytes($GridPath, [byte[]] (9, 8))
    [IO.File]::WriteAllBytes($PackagePath, [byte[]] (1, 2, 3))
    Set-W1FixtureFault $FaultRoot 'mp_atp' 'CorruptPackage'
    Assert-Result ((([IO.File]::ReadAllBytes($PackagePath)) -join ',') -ceq '1,2,2') 'Corruption changes payload bytes while preserving the package size.'
    Set-W1FixtureFault $FaultRoot 'mp_atp' 'MissingPackage'
    Assert-Result (!(Test-Path -LiteralPath $PackagePath) -and (Test-Path -LiteralPath $GridPath)) 'Missing-package injection leaves the grid intact.'
    Set-W1FixtureFault $FaultRoot 'mp_atp' 'MissingGrid'
    Assert-Result (!(Test-Path -LiteralPath $GridPath)) 'Missing-grid injection removes the staged grid.'
} finally {
    Remove-Item -LiteralPath $FaultRoot -Force -Recurse
}

$TransportOnly = "[SteamNetClient] Connected to server`n[SteamNetServer] server client connected"
Assert-Result (@(Get-W0ReadyPeers $TransportOnly).Count -eq 0) 'Transport connections do not establish DM readiness.'
$Peers = @(Get-W0ReadyPeers "DM ready: role=server peer=1 pid=21`r`nDM ready: role=remote peer=2 pid=22`nDM ready: role=remote peer=2 pid=22`nname: DM ready: role=remote peer=3 pid=23")
Assert-Result ($Peers.Count -eq 2 -and $Peers[0].role -ceq 'server' -and $Peers[1].peer -eq 2 -and $Peers[1].pid -eq 22) 'Ready evidence has exact line boundaries, role, identity, and duplicate suppression.'
Assert-Result (@(Get-W0ReadyPeers "DM ready: role=remote peer=4294967296 pid=23`nDM ready: role=remote peer=1 pid=0").Count -eq 0) 'Malformed numeric readiness records are ignored.'
Assert-Result (!(Test-W0RemotePair @() @(22, 23))) 'No readiness can be evaluated while both process logs are still empty.'
Assert-Result (!(Test-W0RemotePair $Peers @(22, 23))) 'The dedicated process does not count as an external client.'
$SecondPeer = [pscustomobject] @{ role = 'remote'; peer = 3; pid = 23 }
Assert-Result (Test-W0RemotePair ($Peers + $SecondPeer) @(22, 23)) 'Two distinct external processes and peer IDs satisfy the join check.'
$SecondPeer.peer = 2
Assert-Result (!(Test-W0RemotePair ($Peers + $SecondPeer) @(22, 23))) 'Two PIDs cannot share one ready peer identity.'

$Root = 'w0-client-1'
$Aliases = @'
FS alias $app_data_root$ = w0-client-1\
FS alias $logs$ = w0-client-1\logs\
FS alias $game_saves$ = w0-client-1\savedgames\
FS alias $screenshots$ = w0-client-1\screenshots\
FS alias $downloads$ = w0-client-1\
'@
Assert-Result (Test-W0WritableAliases $Aliases $Root) 'All five runtime aliases resolve into the requested root.'
Assert-Result (Test-W0WritableAliases ($Aliases.Replace('w0-client-1', 'w0-client-2')) 'w0-client-2') 'The same alias evidence check covers an independent client root.'
Assert-Result (!(Test-W0WritableAliases (($Aliases -split '\r?\n')[0..3] -join "`n") $Root)) 'Four aliases cannot satisfy the isolation check.'
Assert-Result (!(Test-W0WritableAliases ($Aliases.Replace('w0-client-1\savedgames', 'w0-client-10\savedgames')) $Root)) 'An alias must stay inside its own root, including when another path shares the prefix.'
Assert-Result (!(Test-W0WritableAliases ($Aliases + "`n" + 'FS alias $downloads$ = _appdata_ixray_\') $Root)) 'Conflicting alias definitions invalidate the evidence.'

$Report = @{ runtime = @{} }
$Child = [pscustomobject] @{ entry = @{ name = 'first' }; process = [pscustomobject] @{ HasExited = $false } }
$Other = [pscustomobject] @{ entry = @{ name = 'other' }; process = [pscustomobject] @{ HasExited = $false } }
Wait-W0Evidence 'first child evidence' 1 { $Child.entry.name -ceq 'first' } @($Child, $Other)
$Other.process.HasExited = $true
$Other.process | Add-Member -NotePropertyName ExitCode -NotePropertyValue 2
try {
    Wait-W0Evidence 'unreached stage' 1 { $true } @($Child, $Other)
    Assert-Result $false 'A child exit must fail the stage.'
}
catch {
    Assert-Result ($_.Exception.Message -ceq 'other exited with code 2 before unreached stage.') 'A child exit identifies its role and code.'
}

# Supply keyboard decisions and skip real polling sleeps in these fixtures.
function Start-Sleep { param([int] $Milliseconds) }

function Invoke-ObservationFixture {
    param([object[]] $Keys, [int] $DurationSeconds, [object[]] $Children)

    $Report = @{ runtime = @{ timedOut = $false; observation = $null } }
    $Config = @{
        durationSeconds = $DurationSeconds
        checklist = @(
            @{ id = 'fire'; instruction = 'Confirm a shot.' }
            @{ id = 'reconnect'; instruction = 'Reconnect to port {port}.' }
        )
    }
    $Queue = [System.Collections.Generic.Queue[string]]::new([string[]] $Keys)
    $Status = $null
    $ErrorText = $null
    try {
        $Status = Invoke-W0Observation $Config 45678 $Children { $Queue.Dequeue() }
    }
    catch { $ErrorText = $_.Exception.Message }
    [pscustomobject] @{ status = $Status; error = $ErrorText; result = $Report.runtime.observation; timedOut = $Report.runtime.timedOut }
}

$Complete = Invoke-ObservationFixture @('', 'X', 'Y', 'Y') 30 @($Child)
Assert-Result ($Complete.status -ceq 'runtime_observation_passed') 'Every confirmed checklist item is needed for an observation pass.'
Assert-Result ($null -ne $Complete.result -and $Complete.result.finishReason -ceq 'completed' -and
    ($Complete.result.checklist.status -join ',') -ceq 'observed,observed' -and
    $Complete.result.checklist[0].recordedUtc -and $Complete.result.finishedUtc -and
    $Complete.result.checklist[1].instruction -ceq 'Reconnect to port 45678.') 'The report retains the decisions and the tested port.'

$Failed = Invoke-ObservationFixture @('N', 'Y') 30 @($Child)
Assert-Result ($Failed.status -ceq 'runtime_observation_failed' -and $Failed.result.checklist[0].status -ceq 'failed') 'A failed gameplay action stays failed after later success.'
$StoppedAfterFailure = Invoke-ObservationFixture @('N', 'Q') 30 @($Child)
Assert-Result ($StoppedAfterFailure.status -ceq 'runtime_observation_failed' -and $StoppedAfterFailure.result.finishReason -ceq 'operator_stopped') 'Ending after a failure preserves the failure and stop reason.'
$Skipped = Invoke-ObservationFixture @('Y', 'S') 30 @($Child)
Assert-Result ($Skipped.status -ceq 'runtime_observation_incomplete' -and $Skipped.result.checklist[1].status -ceq 'skipped') 'A skipped action leaves the observation incomplete.'
$Stopped = Invoke-ObservationFixture @('Y', 'Q') 30 @($Child)
Assert-Result ($Stopped.status -ceq 'runtime_observation_incomplete' -and $Stopped.result.finishReason -ceq 'operator_stopped' -and
    ($Stopped.result.checklist.status -join ',') -ceq 'observed,unobserved') 'Ending early retains partial results.'
$Expired = Invoke-ObservationFixture @() 0 @($Child)
Assert-Result ($Expired.status -ceq 'runtime_observation_incomplete' -and $Expired.result.finishReason -ceq 'deadline' -and
    $Expired.timedOut -and ($Expired.result.checklist.status -join ',') -ceq 'unobserved,unobserved') 'The deadline leaves a distinct incomplete result without requiring a key.'
$Exited = Invoke-ObservationFixture @('Y', 'Y') 30 @($Child, $Other)
Assert-Result ($Exited.error -ceq 'other exited with code 2 before gameplay observation.' -and $Exited.result.finishReason -ceq 'interrupted') 'A child exit interrupts the checklist as a runtime failure.'

foreach ($Failure in $Failures) { [Console]::Error.WriteLine($Failure) }
if ($Failures.Count) { exit 1 }
Write-Host 'W0 evidence checks passed.'
