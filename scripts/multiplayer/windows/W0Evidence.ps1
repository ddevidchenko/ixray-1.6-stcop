function Get-W0ListenerOutcome {
    param([string] $Output, [int] $Port)

    if (@(Get-W0ReadyPeers $Output).Count -ne 0) { return 'ready' }

    $BindFailed = $false
    foreach ($Line in ($Output -split '\r?\n')) {
        if ($Line -ceq "[SteamNetServer] Failed to listen on port $Port") {
            $BindFailed = $true
        }
        if ($BindFailed -and $Line -ceq 'Failed to start server.') { return 'failed' }
        if ($BindFailed -and $Line.StartsWith('[SteamNetClient] Connecting to server at ', [StringComparison]::Ordinal)) {
            return 'continued'
        }
    }
    return 'pending'
}

function Get-W0ReadyPeers {
    param([string] $Output)

    $Seen = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($Line in ($Output -split '\r?\n')) {
        if ($Line -cmatch '^DM ready: role=(server|remote) peer=([1-9][0-9]{0,9}) pid=([1-9][0-9]{0,9})$') {
            $Peer = [uint32] 0
            $ProcessId = [uint32] 0
            if ([uint32]::TryParse($Matches[2], [ref] $Peer) -and [uint32]::TryParse($Matches[3], [ref] $ProcessId) -and $Seen.Add($Line)) {
                [pscustomobject] @{ role = $Matches[1]; peer = $Peer; pid = $ProcessId }
            }
        }
    }
}

function Test-W0WritableAliases {
    param([string] $Output, [string] $Root)

    $Expected = @{
        '$app_data_root$' = "$Root\"
        '$logs$' = "$Root\logs\"
        '$game_saves$' = "$Root\savedgames\"
        '$screenshots$' = "$Root\screenshots\"
        '$downloads$' = "$Root\"
    }
    $RequiredAliases = @($Expected.Keys)
    foreach ($Line in ($Output -split '\r?\n')) {
        if ($Line -cmatch '^FS alias (\$[a-z_]+\$) = (.+)$' -and $Matches[1] -in $RequiredAliases) {
            if (!$Expected.ContainsKey($Matches[1]) -or $Expected[$Matches[1]] -ine $Matches[2]) { return $false }
            $Expected.Remove($Matches[1])
        }
    }
    return $Expected.Count -eq 0
}

function Test-W0RemotePair {
    param([object[]] $Peers, [uint32[]] $ProcessIds)

    $Remote = @($Peers | Where-Object { $_.role -ceq 'remote' -and $_.pid -in $ProcessIds })
    return $Remote.Count -eq 2 -and $Remote[0].pid -ne $Remote[1].pid -and $Remote[0].peer -ne $Remote[1].peer
}
