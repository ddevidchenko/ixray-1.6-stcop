function Initialize-W0Runtime {
    param([string] $GameDataRoot, [string] $Map, $Config)

    if (!$IsWindows -or ![Environment]::UserInteractive) { throw 'W0 runtime requires an interactive Windows desktop.' }
    if (!$GameDataRoot -or !$Map) { throw 'Specify -GameDataRoot and -Map for W0 runtime.' }
    $Install = (Get-Item -LiteralPath $GameDataRoot).FullName
    $Runtime = (New-Item -ItemType Directory -Path (Join-Path $Bundle 'runtime')).FullName
    $Report.runtime = [ordered] @{ case = $RuntimeCase; map = $Map; inputFiles = @(); port = $null; processes = @(); readyPeers = @(); stage = 'asset preflight'; timedOut = $false }

    $MapDirectory = Join-Path $Install "gamedata/levels/$Map"
    $MapFiles = @('level', 'level.ltx', 'level.geom', 'level.cform') | ForEach-Object { Join-Path $MapDirectory $_ }
    if (@($MapFiles | Where-Object { !(Test-Path -LiteralPath $_ -PathType Leaf) }).Count -ne 0) {
        $MapFiles = @(Get-ChildItem -LiteralPath (Join-Path $Install 'mp') -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "$Map.db*" -or $_.Name -like "$Map.xdb*" } | ForEach-Object FullName)
        if (!$MapFiles.Count) { throw "Missing fixture for $Map. Supply a loose level or a map-named archive in mp/." }
    }

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

    try { return [IO.File]::ReadAllText($Path) }
    catch [IO.IOException] { return '' }
}

function Get-W0EngineLog {
    param([string] $Runtime, [string] $Root)

    $Files = @(Get-ChildItem -LiteralPath (Join-Path $Runtime "$Root/logs") -File -Filter '*.log' -ErrorAction SilentlyContinue)
    if ($Files.Count -eq 1) { return $Files[0].FullName }
}

function Start-W0Process {
    param([string] $Name, [string] $Program, [string[]] $Arguments, [string] $Runtime)

    $Root = "w0-$Name"
    $Stdout = "logs/$Name.stdout.log"
    $Stderr = "logs/$Name.stderr.log"
    $Command = @('-overlaypath', $Root, '-nosound') + $Arguments
    $null = New-Item -ItemType Directory -Path (Join-Path $Runtime $Root)
    $Process = Start-Process -FilePath $Program -WorkingDirectory $Runtime -ArgumentList $Command -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $Bundle $Stdout) -RedirectStandardError (Join-Path $Bundle $Stderr)
    $Entry = [ordered] @{ name = $Name; pid = $Process.Id; arguments = $Command; stdout = $Stdout; stderr = $Stderr; engineLog = $null; exitCode = $null; stoppedByRunner = $false }
    $Report.runtime.processes += $Entry
    return [pscustomobject] @{ process = $Process; entry = $Entry; root = $Root }
}

function Wait-W0Evidence {
    param([string] $Name, [int] $Seconds, [scriptblock] $Condition, [object[]] $Children)

    $Report.runtime.stage = $Name
    $Elapsed = [Diagnostics.Stopwatch]::StartNew()
    do {
        foreach ($OwnedChild in $Children) {
            if ($OwnedChild.process.HasExited) { throw "$($OwnedChild.entry.name) exited with code $($OwnedChild.process.ExitCode) before $Name." }
        }
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 200
    } while ($Elapsed.Elapsed.TotalSeconds -lt $Seconds)
    $Report.runtime.timedOut = $true
    throw "Timed out waiting for $Name after $Seconds seconds."
}

function Invoke-W0Runtime {
    param([string] $Runtime, [string] $Map, $Config, [string] $Case)

    $BinaryDirectory = Join-Path $Source $Config.binaryDirectory
    $ServerExecutable = Join-Path $BinaryDirectory 'xrServerCLI.exe'
    $ClientExecutable = Join-Path $BinaryDirectory 'xrEngine.exe'
    $Children = @()
    $Reservation = [Net.Sockets.Socket]::new([Net.Sockets.AddressFamily]::InterNetworkV6, [Net.Sockets.SocketType]::Dgram, [Net.Sockets.ProtocolType]::Udp)
    try {
        $Reservation.DualMode = $true
        $Reservation.ExclusiveAddressUse = $true
        $Reservation.Bind([Net.IPEndPoint]::new([Net.IPAddress]::IPv6Any, 0))
        $Port = $Reservation.LocalEndPoint.Port
        $Report.runtime.port = $Port
        if ($Case -eq 'Join') { $Reservation.Dispose() }

        $Server = Start-W0Process 'server' $ServerExecutable @('-start', "server($Map/deathmatch/maxplayers=32/portsv=$Port/public=0)", "client(localhost/port=$Port/name=W0Server)") $Runtime
        $Children += $Server
        $ServerOutput = Join-Path $Bundle $Server.entry.stdout

        if ($Case -eq 'ListenerFailure') {
            Wait-W0Evidence 'propagated listener failure on redirected stdout' $Config.serverTimeoutSeconds {
                Test-W0ListenerFailure (Get-W0Log $ServerOutput) $Port
            } $Children
            return 'listener_failure_observed'
        }

        Wait-W0Evidence 'dedicated DM server readiness' $Config.serverTimeoutSeconds {
            $Output = Get-W0Log $ServerOutput
            if ($Output -cmatch '(?m)^Failed to start server\.\r?$') { throw 'The server reported startup failure. See server.stdout.log.' }
            @(Get-W0ReadyPeers $Output | Where-Object { $_.role -ceq 'server' -and $_.pid -eq $Server.process.Id }).Count -eq 1
        } $Children

        foreach ($Index in 1..2) {
            $Children += Start-W0Process "client-$Index" $ClientExecutable @('-r2', '-start', "client(127.0.0.1/port=$Port/name=W0Client$Index)") $Runtime
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
