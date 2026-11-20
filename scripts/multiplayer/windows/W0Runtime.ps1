function Initialize-W0Runtime {
    param([string] $GameDataRoot, [string] $Map, $Config, [string] $Workspace)

    if (!$IsWindows -or ![Environment]::UserInteractive) { throw 'W0 runtime requires an interactive Windows desktop.' }
    if ($RuntimeCase -eq 'Observe' -and [Console]::IsInputRedirected) { throw 'Run W0 Observe from a PowerShell terminal with keyboard input.' }
    if (!$GameDataRoot -or !$Map) { throw 'Specify -GameDataRoot and -Map for W0 runtime.' }
    $InstallItem = Get-Item -LiteralPath $GameDataRoot
    if (!$InstallItem.PSIsContainer -or ($InstallItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Use a physical game-data directory for -GameDataRoot.'
    }
    $Install = $InstallItem.FullName
    $InstallPrefix = [IO.Path]::TrimEndingDirectorySeparator($Install) + [IO.Path]::DirectorySeparatorChar
    if ($Bundle.StartsWith($InstallPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Use a results root outside the game-data installation.'
    }
    if ($Workspace.StartsWith($InstallPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Use a workspace root outside the game-data installation.'
    }
    $Report.runtime = [ordered] @{ case = $RuntimeCase; map = $Map; inputFiles = @(); port = $null; processes = @(); readyPeers = @(); startupPassed = $false; stage = 'asset preflight'; timedOut = $false }

    $MapDirectory = Join-Path $Install "gamedata/levels/$Map"
    $MapFiles = @('level', 'level.ltx', 'level.geom', 'level.cform') | ForEach-Object { Join-Path $MapDirectory $_ }
    if (@($MapFiles | Where-Object { !(Test-Path -LiteralPath $_ -PathType Leaf) }).Count -ne 0) {
        $MapFiles = @(Get-ChildItem -LiteralPath (Join-Path $Install 'mp') -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "$Map.db*" -or $_.Name -like "$Map.xdb*" } | ForEach-Object FullName)
        if (!$MapFiles.Count) { throw "Missing fixture for $Map. Supply a loose level or a map-named archive in mp/." }
    }

    if ($RuntimeCase -eq 'StalkerNavigation') {
        $Report.workItems = $Config.stalkerNavigation.workItems
        $Report.runtime.navigationStatus = 'pending'
        if (Test-Path -LiteralPath (Join-Path $MapDirectory 'alife.spawn')) {
            throw 'Use the W1 staging script to prepare a map with level.ai and no alife.spawn.'
        }
        foreach ($Name in $Config.stalkerNavigation.files) {
            $Path = Join-Path $MapDirectory $Name
            $Asset = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
            if (!$Asset -or $Asset.PSIsContainer -or ($Asset.Attributes -band [IO.FileAttributes]::ReparsePoint) -or !$Asset.Length) {
                throw "W1 navigation fixture requires a nonempty loose file at $Path."
            }
            $MapFiles += $Asset.FullName
        }
        if ($Config.stalkerNavigation.files -contains 'level.navlinks') {
            $MapFiles += @(Get-ChildItem -LiteralPath $MapDirectory -File | Where-Object {
                $_.Name -in @('level.spawn', 'w1-fixture.json') -or $_.Name -match '^level_\d+\.cform$'
            } | ForEach-Object FullName)
            $Materials = Join-Path $Install 'gamedata/gamemtl.xr'
            if (Test-Path -LiteralPath $Materials -PathType Leaf) {
                $MapFiles += $Materials
            }
        }
    }

    $Runtime = (New-Item -ItemType Directory -Path (Join-Path $Workspace 'runtime')).FullName
    $Robocopy = (Get-Command robocopy -CommandType Application | Select-Object -First 1).Source
    $CopyFlags = @('/XJ', '/R:0', '/W:0', '/NFL', '/NDL', '/NP')
    foreach ($Directory in $Config.assetDirectories) {
        $InputPath = Join-Path $Install $Directory
        if (!(Test-Path -LiteralPath $InputPath -PathType Container)) { continue }
        if ((Get-Item -LiteralPath $InputPath).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Use a physical asset directory for $InputPath."
        }
        $OutputPath = Join-Path $Runtime $Directory
        $null = Invoke-CheckpointStep "stage-$Directory" $Robocopy (@($InputPath, $OutputPath, '/E') + $CopyFlags) $Bundle $Config.assetCopyTimeoutSeconds (0..7)
    }
    $null = Invoke-CheckpointStep 'stage-root-archives' $Robocopy (@($Install, $Runtime, '*.db*', '*.xdb*') + $CopyFlags) $Bundle $Config.assetCopyTimeoutSeconds (0..7)
    Copy-Item -LiteralPath (Join-Path $Source 'fsgame.ltx'), (Join-Path $Source '.xrignore') -Destination $Runtime

    $Report.runtime.inputFiles = @($MapFiles | ForEach-Object {
        $RelativePath = [IO.Path]::GetRelativePath($Install, $_)
        $Hash = (Get-FileHash -LiteralPath (Join-Path $Runtime $RelativePath) -Algorithm SHA256).Hash
        [ordered] @{ path = $RelativePath; sha256 = $Hash }
    })
    return $Runtime
}

function Get-W0Log {
    param([string] $Path)

    try {
        $File = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            $Reader = [IO.StreamReader]::new($File)
            try { return $Reader.ReadToEnd() }
            finally { $Reader.Dispose() }
        }
        finally { $File.Dispose() }
    }
    catch [IO.IOException] { return '' }
}

function Get-W0EngineLog {
    param([string] $Runtime, [string] $Root)

    $Files = @(Get-ChildItem -LiteralPath (Join-Path $Runtime "$Root/logs") -File -Filter '*.log' -ErrorAction SilentlyContinue)
    if ($Files.Count -eq 1) { return $Files[0].FullName }
}

function Start-W0Process {
    param([string] $Name, [string] $Program, [string[]] $Arguments, [string] $Runtime, [string[]] $UserSettings)

    $Root = "w0-$Name"
    $Stdout = "logs/$Name.stdout.log"
    $Stderr = "logs/$Name.stderr.log"
    $Command = @('-overlaypath', $Root, '-nosound') + $Arguments
    $null = New-Item -ItemType Directory -Path (Join-Path $Runtime $Root)
    if ($UserSettings) {
        Set-Content -LiteralPath (Join-Path $Runtime "$Root/user.ltx") -Value $UserSettings -Encoding ascii
    }
    $InputFile = Join-Path $Runtime "$Root/stdin.txt"
    $null = New-Item -ItemType File -Path $InputFile
    $Launch = @{
        FilePath = $Program
        WorkingDirectory = $Runtime
        ArgumentList = $Command
        NoNewWindow = $true
        PassThru = $true
        RedirectStandardInput = $InputFile
        RedirectStandardOutput = Join-Path $Bundle $Stdout
        RedirectStandardError = Join-Path $Bundle $Stderr
    }
    $Process = Start-Process @Launch
    $Entry = [ordered] @{ name = $Name; pid = $Process.Id; arguments = $Command; stdout = $Stdout; stderr = $Stderr; engineLog = $null; exitCode = $null; stoppedByRunner = $false }
    $Report.runtime.processes += $Entry
    return [pscustomobject] @{ process = $Process; entry = $Entry; root = $Root }
}

function Wait-W0Evidence {
    param([string] $Name, [int] $Seconds, [scriptblock] $Condition, [object[]] $Children)

    $Report.runtime.stage = $Name
    $Elapsed = [Diagnostics.Stopwatch]::StartNew()
    while ($Elapsed.Elapsed.TotalSeconds -lt $Seconds) {
        foreach ($OwnedChild in $Children) {
            if ($OwnedChild.process.HasExited) { throw "$($OwnedChild.entry.name) exited with code $($OwnedChild.process.ExitCode) before $Name." }
        }
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 200
    }
    $Report.runtime.timedOut = $true
    throw "Timed out waiting for $Name after $Seconds seconds."
}

function Invoke-W0Observation {
    param(
        $Config,
        [int] $Port,
        [object[]] $Children,
        [scriptblock] $ReadKey = { if ([Console]::KeyAvailable) { [Console]::ReadKey($true).Key } }
    )

    $Checklist = @($Config.checklist | ForEach-Object {
        [ordered] @{
            id = $_.id
            instruction = $_.instruction.Replace('{port}', [string] $Port)
            status = 'unobserved'
            recordedUtc = $null
        }
    })
    if (!$Checklist.Count) { throw 'W0 Observe requires a gameplay checklist.' }

    $Observation = [ordered] @{
        durationSeconds = $Config.durationSeconds
        startedUtc = [DateTimeOffset]::UtcNow.ToString('o')
        finishedUtc = $null
        finishReason = 'interrupted'
        checklist = $Checklist
    }
    $Report.runtime.observation = $Observation
    Write-Host "W0 gameplay observation has $($Config.durationSeconds) seconds. Return to this terminal after each step."
    Write-Host 'Press Y for observed, N for failed, S to skip, or Q to finish early.'
    Write-Host $Checklist[0].instruction

    try {
        Wait-W0Evidence 'gameplay observation' $Config.durationSeconds {
            $Key = & $ReadKey
            if ($Key -eq 'Q') {
                $Observation.finishReason = 'operator_stopped'
                return $true
            }
            $Decision = switch ($Key) {
                'Y' { 'observed' }
                'N' { 'failed' }
                'S' { 'skipped' }
            }
            if (!$Decision) { return $false }

            $Unobserved = @($Checklist | Where-Object { $_.status -eq 'unobserved' })
            $Unobserved[0].status = $Decision
            $Unobserved[0].recordedUtc = [DateTimeOffset]::UtcNow.ToString('o')
            if ($Unobserved.Count -eq 1) {
                $Observation.finishReason = 'completed'
                return $true
            }
            Write-Host $Unobserved[1].instruction
            return $false
        } $Children
    }
    catch {
        if (!$Report.runtime.timedOut) { throw }
        $Observation.finishReason = 'deadline'
        Write-Host 'W0 gameplay observation reached its deadline. The report includes the decisions recorded so far.'
    }
    finally {
        $Observation.finishedUtc = [DateTimeOffset]::UtcNow.ToString('o')
    }

    if ($Checklist.status -contains 'failed') { return 'runtime_observation_failed' }
    if (@($Checklist | Where-Object { $_.status -ne 'observed' }).Count) { return 'runtime_observation_incomplete' }
    return 'runtime_observation_passed'
}

function Set-W1FixtureFault {
    param([string] $Runtime, [string] $Map, [string] $Fault)

    $MapDirectory = Join-Path $Runtime "gamedata/levels/$Map"
    $Package = Join-Path $MapDirectory 'level.navlinks'
    switch ($Fault) {
        'MissingGrid' { Remove-Item -LiteralPath (Join-Path $MapDirectory 'level.ai') }
        'MissingPackage' { Remove-Item -LiteralPath $Package }
        'CorruptPackage' {
            $Bytes = [IO.File]::ReadAllBytes($Package)
            $Bytes[$Bytes.Length - 1] = $Bytes[$Bytes.Length - 1] -bxor 1
            [IO.File]::WriteAllBytes($Package, $Bytes)
        }
        default { throw "Unknown navigation fault $Fault." }
    }
}

function Invoke-W0Runtime {
    param([string] $Runtime, [string] $Map, $Config, [string] $Case, [string] $BinaryDirectory, [string] $NavigationFault = 'None')

    $ServerExecutable = Join-Path $BinaryDirectory 'xrServerCLI.exe'
    $ClientExecutable = Join-Path $BinaryDirectory 'xrEngine.exe'
    $Children = @()
    $PreListenerCheck = $Case -eq 'StalkerNavigation' -and $Config.stalkerNavigation.PSObject.Properties['preListener'] -and
        $Config.stalkerNavigation.preListener
    if ($NavigationFault -ne 'None') {
        if (!$PreListenerCheck) { throw 'This candidate does not support the dedicated navigation rejection check.' }
        $Report.runtime.navigationFault = $NavigationFault
        Set-W1FixtureFault $Runtime $Map $NavigationFault
    }
    $Reservation = [Net.Sockets.Socket]::new([Net.Sockets.AddressFamily]::InterNetworkV6, [Net.Sockets.SocketType]::Dgram, [Net.Sockets.ProtocolType]::Udp)
    try {
        $Reservation.DualMode = $true
        $Reservation.ExclusiveAddressUse = $true
        $Reservation.Bind([Net.IPEndPoint]::new([Net.IPAddress]::IPv6Any, 0))
        $Port = $Reservation.LocalEndPoint.Port
        $Report.runtime.port = $Port
        if ($Case -ne 'ListenerFailure') { $Reservation.Dispose() }

        $ServerOptions = "$Map/deathmatch/maxplayers=32/portsv=$Port/public=0"
        if ($Case -eq 'StalkerNavigation') {
            $ServerOptions += '/' + $Config.stalkerNavigation.serverOption
        }
        $Server = Start-W0Process 'server' $ServerExecutable @('-start', "server($ServerOptions)", "client(localhost/port=$Port/name=W0Server)") $Runtime
        $Children += $Server
        $ServerOutput = Join-Path $Bundle $Server.entry.stdout

        if ($Case -eq 'ListenerFailure') {
            Wait-W0Evidence 'propagated listener failure on redirected stdout' $Config.serverTimeoutSeconds {
                $Outcome = Get-W0ListenerOutcome (Get-W0Log $ServerOutput) $Port
                if ($Outcome -eq 'continued') { throw 'The internal client attempted to connect after the listener failed.' }
                if ($Outcome -eq 'ready') { throw 'The server reached DM readiness while the fixture held its port.' }
                $Outcome -eq 'failed'
            } $Children
            return 'listener_failure_observed'
        }

        if ($NavigationFault -ne 'None') {
            Wait-W0Evidence 'navigation rejection before the listener' $Config.serverTimeoutSeconds {
                $Outcome = Get-W1RejectionOutcome (Get-W0Log $ServerOutput) $Map $NavigationFault
                if ($Outcome -ceq 'continued') { throw 'The server started its listener or attempted a connection with invalid navigation.' }
                $Outcome -ceq 'rejected'
            } $Children
            $Report.runtime.navigationStatus = 'failed'
            return 'w1_navigation_rejected'
        }

        Wait-W0Evidence 'dedicated DM server readiness' $Config.serverTimeoutSeconds {
            $Output = Get-W0Log $ServerOutput
            $ServerReady = @(Get-W0ReadyPeers $Output | Where-Object { $_.role -ceq 'server' -and $_.pid -eq $Server.process.Id }).Count -eq 1
            if ($Case -eq 'StalkerNavigation') {
                $Navigation = Get-W1NavigationOutcome $Output $Map $ServerReady $PreListenerCheck
                $Report.runtime.navigationStatus = $Navigation
                if ($Navigation -ceq 'failed') { throw 'The dedicated server rejected the W1 navigation fixture. See server.stdout.log.' }
                if ($Navigation -cin @('not_loaded', 'late', 'not_listening')) {
                    throw 'The dedicated server did not prove navigation was ready before the listener. See server.stdout.log.'
                }
            }
            if ($Output -cmatch '(?m)^Failed to start server\.\r?$') { throw 'The server reported startup failure. See server.stdout.log.' }
            return $ServerReady
        } $Children

        foreach ($Index in 1..2) {
            $Children += Start-W0Process "client-$Index" $ClientExecutable @('-r2', '-start', "client(127.0.0.1/port=$Port/name=W0Client$Index)") $Runtime @('rs_fullscreen 0')
        }
        $Clients = $Children[1..2]
        Wait-W0Evidence 'two distinct remote DM peers' $Config.clientTimeoutSeconds {
            $Ready = @(Get-W0ReadyPeers (Get-W0Log $ServerOutput) | Where-Object { $_.role -ceq 'remote' -and $_.pid -in $Clients.process.Id })
            $Report.runtime.readyPeers = $Ready
            Test-W0RemotePair $Ready $Clients.process.Id
        } $Children

        foreach ($Child in $Children) {
            Wait-W0Evidence "isolated aliases for $($Child.entry.name)" $Config.logTimeoutSeconds {
                $EngineLog = Get-W0EngineLog $Runtime $Child.root
                $EngineLog -and (Test-W0WritableAliases (Get-W0Log $EngineLog) $Child.root)
            } $Children
        }
        $Report.runtime.startupPassed = $true
        if ($Case -eq 'StalkerNavigation') { return 'w1_navigation_passed' }
        if ($Case -eq 'Observe') {
            $Report.runtime.observationNotes = 'observation-notes.txt'
            $NotesPath = Join-Path $Bundle $Report.runtime.observationNotes
            Set-Content -LiteralPath $NotesPath -Encoding utf8 -Value @(
                'Optional notes for the W0 gameplay observation',
                'Record which action failed or was skipped, what each client saw, and any visible startup or service messages.'
            )
            Write-Host "W0 notes $NotesPath"
            return Invoke-W0Observation $Config.observation $Port $Children
        }
        return 'runtime_smoke_passed'
    }
    finally {
        $Reservation.Dispose()
        foreach ($Child in $Children) {
            try {
                if (!$Child.process.HasExited) {
                    try { $Child.process.Kill($true); $Child.entry.stoppedByRunner = $true }
                    catch [InvalidOperationException] { if (!$Child.process.HasExited) { throw } }
                }
                if (!$Child.process.WaitForExit(5000)) { throw "Timed out stopping $($Child.entry.name)." }
                $Child.entry.exitCode = $Child.process.ExitCode
                $EngineLog = Get-W0EngineLog $Runtime $Child.root
                if ($EngineLog) {
                    $Child.entry.engineLog = "logs/$($Child.entry.name).engine.log"
                    Copy-Item -LiteralPath $EngineLog -Destination (Join-Path $Bundle $Child.entry.engineLog)
                }
            }
            catch { $Child.entry.cleanupError = $_.Exception.Message }
            finally { $Child.process.Dispose() }
        }
        if (@($Children | Where-Object { $_.entry.Contains('cleanupError') }).Count) { throw 'Process cleanup or engine log collection failed. See the process records.' }
    }
}
