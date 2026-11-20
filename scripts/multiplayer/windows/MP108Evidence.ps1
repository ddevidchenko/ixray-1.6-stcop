function Get-MP108RouteEvidence {
    param([string] $Output, [string] $Map)

    $Generation = [uint64] 0
    $Active = $false
    $DirectPlacement = $false
    $Stalkers = @{}
    foreach ($Line in ($Output -split '\r?\n')) {
        if ($Line -cmatch '^Arena navigation generation: map=([^ ]+) (ready|closing)=([1-9][0-9]*)$') {
            $Seen = [uint64] 0
            if (![uint64]::TryParse($Matches[3], [ref] $Seen)) { continue }
            if ($Matches[1] -cne $Map) {
                if ($Matches[2] -ceq 'ready' -and $Seen -gt $Generation) { $Active = $false }
                continue
            }
            if ($Matches[2] -ceq 'closing' -and $Seen -eq $Generation) { $Active = $false }
            if ($Matches[2] -ceq 'ready' -and $Seen -gt $Generation) {
                $Generation = $Seen
                $Active = $true
                $DirectPlacement = $false
                $Stalkers.Clear()
            }
            continue
        }
        if (!$Active) { continue }

        if ($Line -cmatch '^MP108 walk: generation=([1-9][0-9]*) stalker=([0-9]+) method=(checked|direct)$') {
            $Id = [uint16] 0
            if ($Matches[1] -cne [string] $Generation -or ![uint16]::TryParse($Matches[2], [ref] $Id)) { continue }
            if ($Matches[3] -ceq 'direct') {
                $DirectPlacement = $true
                continue
            }
            $Stalker = [string] $Id
            if (!$Stalkers.ContainsKey($Stalker)) {
                $Stalkers[$Stalker] = @{ attempt = $null; completed = @{}; blocked = @{}; recovered = @{} }
            }
            $Route = $Stalkers[$Stalker]
            $Attempt = $Route.attempt
            # A walk sample after ground contact confirms walking resumed for this dismount.
            if ($Attempt -and $Attempt.phase -ceq 'grounded exit') {
                $Route.completed[$Attempt.pair] = $Attempt.link
                if ($Route.blocked.ContainsKey($Attempt.key)) { $Route.recovered[$Attempt.pair] = $Attempt.link }
                $Route.attempt = $null
            }
            continue
        }

        if ($Line -cmatch '^Arena ladder: generation=([1-9][0-9]*) stalker=([0-9]+) link=([0-9]+) ([0-9]+)->([0-9]+) mount$') {
            if ($Matches[1] -cne [string] $Generation) { continue }
            $Route = $Stalkers[$Matches[2]]
            if (!$Route -or $Matches[4] -ceq $Matches[5]) { continue }
            $Link = $Matches[3]
            $Pair = "$($Matches[4]):$($Matches[5])"
            $Route.attempt = @{ link = $Link; pair = $Pair; key = "$Link/$Pair"; phase = 'mount' }
            continue
        }
        if ($Line -cmatch '^Arena ladder: generation=([1-9][0-9]*) stalker=([0-9]+) link=([0-9]+) (climb|dismount|grounded exit|failed)$') {
            if ($Matches[1] -cne [string] $Generation) { continue }
            $Route = $Stalkers[$Matches[2]]
            $Link = $Matches[3]
            $Phase = $Matches[4]
            if (!$Route -or !$Route.attempt -or $Route.attempt.link -cne $Link) { continue }
            $Attempt = $Route.attempt
            $Expected = switch ($Attempt.phase) {
                'mount' { 'climb' }
                'climb' { 'dismount' }
                'dismount' { 'grounded exit' }
            }
            if ($Phase -ceq $Expected) {
                $Attempt.phase = $Phase
            } else {
                if ($Phase -ceq 'failed' -and $Attempt.phase -ceq 'dismount') {
                    $Route.blocked[$Attempt.key] = $true
                }
                $Route.attempt = $null
            }
        }
    }

    if ($DirectPlacement) {
        return [pscustomobject] @{ status = 'direct_placement'; generation = $Generation; stalker = $null }
    }
    foreach ($Stalker in ($Stalkers.Keys | Sort-Object)) {
        $Route = $Stalkers[$Stalker]
        foreach ($Pair in $Route.recovered.Keys) {
            $Entry, $Exit = $Pair -split ':'
            $Reverse = "${Exit}:$Entry"
            if ($Route.completed[$Pair] -ceq $Route.recovered[$Pair] -and $Route.completed.ContainsKey($Reverse) -and
                $Route.completed[$Pair] -cne $Route.completed[$Reverse]) {
                return [pscustomobject] @{ status = 'observed'; generation = $Generation; stalker = [uint16] $Stalker }
            }
        }
    }
    return [pscustomobject] @{ status = 'incomplete'; generation = $Generation; stalker = $null }
}

function Get-MP108RouteRun {
    param([string] $Output, [string] $Map)

    $Ready = [regex]::Matches($Output, '(?m)^Arena navigation generation: map=([^ ]+) ready=[1-9][0-9]*\r?$')
    $First = if ($Ready.Count -gt 0 -and $Ready[0].Groups[1].Value -ceq $Map) {
        $End = if ($Ready.Count -gt 1) { $Ready[1].Index } else { $Output.Length }
        $Output.Substring($Ready[0].Index, $End - $Ready[0].Index)
    }
    $Second = if ($Ready.Count -eq 2 -and $Ready[1].Groups[1].Value -ceq $Map) {
        $Output.Substring($Ready[1].Index)
    }

    $Fresh = Get-MP108RouteEvidence $First $Map
    $Reloaded = Get-MP108RouteEvidence $Second $Map
    $Close = '(?m)^Arena navigation generation: map=' + [regex]::Escape($Map) + " closing=$($Fresh.generation)\r?$"
    $MatchingClose = [regex]::Match([string] $First, $Close)
    $BeforeClose = if ($MatchingClose.Success) { $First.Substring(0, $MatchingClose.Index) }
    $FirstPeers = @(Get-W0ReadyPeers $BeforeClose | Where-Object role -CEQ 'server')
    $SecondPeers = @(Get-W0ReadyPeers $Second | Where-Object role -CEQ 'server')
    $SameServerReload = $Reloaded.generation -gt $Fresh.generation -and $MatchingClose.Success -and
        $FirstPeers.Count -eq 1 -and $SecondPeers.Count -eq 1 -and $FirstPeers[0].pid -eq $SecondPeers[0].pid
    [pscustomobject] @{ fresh = $Fresh; reloaded = $Reloaded; sameServerReload = [bool] $SameServerReload }
}

function Get-MP108RouteChecks {
    'stairs', 'doorway', 'closed_door', 'crowd', 'ladder_blocked', 'ladder_up', 'ladder_down', 'unreachable'
}

function Get-MP108RouteObservationOutcome {
    param([System.Collections.IDictionary] $Checks, [string] $RouteStatus, [bool] $HasRecording)

    $AllChecks = Get-MP108RouteChecks
    foreach ($Check in $AllChecks) {
        if ($Checks[$Check] -ceq 'fail') { return 'failed' }
    }
    if ($RouteStatus -ceq 'direct_placement') { return 'failed' }
    if (!$HasRecording -or $RouteStatus -cne 'observed') { return 'incomplete' }
    foreach ($Check in $AllChecks) {
        if ($Checks[$Check] -cne 'pass') { return 'incomplete' }
    }
    return 'observed'
}
