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
Assert-Result (!(Test-W0ListenerFailure $BindFailure 5446)) 'A low-level bind error needs a propagated startup failure.'
Assert-Result (Test-W0ListenerFailure "$BindFailure`n$StartupFailure" 5446) 'An ordered bind and startup failure is the intended regression outcome.'
Assert-Result (!(Test-W0ListenerFailure "$StartupFailure`n$BindFailure" 5446)) 'A prior startup failure does not explain a later bind failure.'
Assert-Result (!(Test-W0ListenerFailure "${BindFailure}0`n$StartupFailure" 5446)) 'A port prefix is insufficient.'
Assert-Result (!(Test-W0ListenerFailure "$BindFailure`n$StartupFailure`nDM ready: role=server peer=1 pid=21" 5446)) 'A ready server contradicts the failed startup.'

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

foreach ($Failure in $Failures) { [Console]::Error.WriteLine($Failure) }
if ($Failures.Count) { exit 1 }
Write-Host 'W0 evidence checks passed.'
