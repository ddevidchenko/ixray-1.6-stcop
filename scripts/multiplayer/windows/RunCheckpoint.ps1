#Requires -Version 7.4

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string] $Revision,

    [ValidateSet('Portable', 'Peers', 'BaselineBuild', 'BaselineRuntime')]
    [string] $Mode = 'Portable',

    [string] $ResultsRoot,
    [string] $GameDataRoot,
    [ValidatePattern('^[a-z0-9_-]{1,63}$')]
    [string] $Map,
    [ValidateSet('Join', 'ListenerFailure', 'Observe', 'StalkerNavigation')]
    [string] $RuntimeCase = 'Join',
    [ValidateSet('None', 'MissingGrid', 'MissingPackage', 'CorruptPackage')]
    [string] $NavigationFault = 'None',
    [switch] $Shipping,
    [string] $WorkspaceRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (($NavigationFault -ne 'None' -or $Shipping) -and ($Mode -ne 'BaselineRuntime' -or $RuntimeCase -ne 'StalkerNavigation')) {
    throw '-NavigationFault and -Shipping require -Mode BaselineRuntime -RuntimeCase StalkerNavigation.'
}

function Invoke-CheckpointStep {
    param(
        [string] $Name,
        [string] $Program,
        [string[]] $Arguments,
        [string] $WorkingDirectory,
        [int] $TimeoutSeconds,
        [int[]] $ExpectedExitCodes = @(0)
    )

    $Step = [ordered] @{
        name = $Name
        program = $Program
        arguments = $Arguments
        exitCode = $null
        timedOut = $false
        stdout = "logs/$Name.stdout.log"
        stderr = "logs/$Name.stderr.log"
    }
    $Report.steps.Add($Step)

    $Process = [System.Diagnostics.Process]::new()
    $Process.StartInfo.FileName = $Program
    $Process.StartInfo.WorkingDirectory = $WorkingDirectory
    $Process.StartInfo.UseShellExecute = $false
    $Process.StartInfo.RedirectStandardOutput = $true
    $Process.StartInfo.RedirectStandardError = $true
    foreach ($Argument in $Arguments) {
        $Process.StartInfo.ArgumentList.Add($Argument)
    }

    $Started = $false
    $Stdout = $null
    $Stderr = $null
    try {
        Write-Host "W0 $Name"
        $Stdout = [System.IO.File]::Create((Join-Path $Bundle $Step.stdout))
        $Stderr = [System.IO.File]::Create((Join-Path $Bundle $Step.stderr))
        $Started = $Process.Start()
        $Output = $Process.StandardOutput.BaseStream.CopyToAsync($Stdout)
        $Errors = $Process.StandardError.BaseStream.CopyToAsync($Stderr)

        if (!$Process.WaitForExit($TimeoutSeconds * 1000)) {
            $Step.timedOut = $true
            throw "$Name exceeded its $TimeoutSeconds-second deadline."
        }
        $Step.exitCode = $Process.ExitCode
        if (![System.Threading.Tasks.Task]::WaitAll(@($Output, $Errors), 5000)) {
            throw "$Name exited but its output streams did not close."
        }
        if ($Step.exitCode -notin $ExpectedExitCodes) {
            throw "$Name exited with code $($Step.exitCode). See $($Step.stdout) and $($Step.stderr)."
        }
    }
    finally {
        if ($Started -and !$Process.HasExited) {
            try {
                $Process.Kill($true)
                [void] $Process.WaitForExit(5000)
            }
            catch [System.InvalidOperationException] {
                if (!$Process.HasExited) {
                    throw
                }
            }
        }
        $Process.Dispose()
        if ($Stdout) { $Stdout.Dispose() }
        if ($Stderr) { $Stderr.Dispose() }
    }

    return $Step
}

function Read-CheckpointTest {
    param([string] $Path, [string] $Name)

    [xml] $Results = Get-Content -Raw -LiteralPath $Path
    $Cases = $Results.SelectNodes('/testsuite/testcase')
    if ($Cases.Count -ne 1 -or $Cases.Item(0).GetAttribute('name') -cne $Name) {
        throw "Expected exactly one result for $Name in $Path."
    }
    return $Cases.Item(0)
}

function Invoke-AssertedCTest {
    param([string] $CTest, [string[]] $Arguments, [string] $Source, [int] $TimeoutSeconds, $Test)

    $Pattern = '^' + [regex]::Escape($Test.name) + '$'
    $Results = Join-Path $Bundle "$($Test.step).xml"
    $null = Invoke-CheckpointStep $Test.step $CTest ($Arguments + @('-R', $Pattern, '--output-junit', $Results)) $Source $TimeoutSeconds
    $Result = Read-CheckpointTest $Results $Test.name
    $Output = $Result.SelectSingleNode('system-out')
    if ($Result.GetAttribute('status') -cne 'run' -or !$Output -or
        ($Output.InnerText -split '\r?\n') -cnotcontains $Test.successMessage) {
        throw "$($Test.name) did not report completion. See $($Test.step).xml."
    }
}

$ControllerRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$Revision = $Revision.ToLowerInvariant()
if (!$ResultsRoot) {
    $ResultsRoot = Join-Path $ControllerRoot 'build/test-results/multiplayer'
}
if (!$WorkspaceRoot) {
    $WorkspaceRoot = Join-Path $ControllerRoot 'build/w0'
}
$RunSuffix = [guid]::NewGuid().ToString('N').Substring(0, 8)
$RunId = (Get-Date -AsUTC -Format 'yyyyMMddTHHmmss') + '-' + $RunSuffix
$Bundle = (New-Item -ItemType Directory -Path (Join-Path $ResultsRoot "$Revision/$RunId/logs") -Force).Parent.FullName
$ReportPath = Join-Path $Bundle 'report.json'
$Report = [ordered] @{
    checkpoint = 'W0'
    mode = $Mode
    requestedRevision = $Revision
    actualRevision = $null
    configuration = $null
    workspaceDirectory = $null
    buildDirectory = $null
    workItems = @()
    platform = [System.Runtime.InteropServices.RuntimeInformation]::OSDescription
    architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    powerShellVersion = $PSVersionTable.PSVersion.ToString()
    status = 'failed'
    error = $null
    steps = [System.Collections.Generic.List[object]]::new()
}

try {
    $DefinitionPath = 'src/test/multiplayer/checkpoints/W0.json'
    $Git = (Get-Command git -CommandType Application | Select-Object -First 1).Source
    $CMake = (Get-Command cmake -CommandType Application | Select-Object -First 1).Source
    if ($Mode -in @('Portable', 'Peers')) {
        $CTest = (Get-Command ctest -CommandType Application | Select-Object -First 1).Source
    }

    $null = New-Item -ItemType Directory -Path $WorkspaceRoot -Force
    $Workspace = (New-Item -ItemType Directory -Path (Join-Path $WorkspaceRoot $RunSuffix)).FullName
    $Report.workspaceDirectory = $Workspace
    $Source = (New-Item -ItemType Directory -Path (Join-Path $Workspace 's')).FullName
    $Build = Join-Path $Workspace 'b'
    $Report.buildDirectory = $Build
    Write-Host "W0 workspace $Workspace"

    $GitDeadline = 900
    $null = Invoke-CheckpointStep 'init' $Git @('init', '--quiet', '--template=') $Source 30
    $null = Invoke-CheckpointStep 'fetch' $Git @('fetch', '--no-tags', '--no-recurse-submodules', '--depth=1', '--', $ControllerRoot, $Revision) $Source $GitDeadline
    $null = Invoke-CheckpointStep 'checkout' $Git @('-c', 'core.longpaths=true', 'checkout', '--detach', 'FETCH_HEAD') $Source $GitDeadline
    $Head = Invoke-CheckpointStep 'verify-revision' $Git @('rev-parse', 'HEAD') $Source 30
    $Report.actualRevision = (Get-Content -Raw -LiteralPath (Join-Path $Bundle $Head.stdout)).Trim()
    if ($Report.actualRevision -cne $Revision) {
        throw "Expected revision $Revision but checked out $($Report.actualRevision)."
    }
    $Status = Invoke-CheckpointStep 'verify-clean' $Git @('-c', 'core.longpaths=true', 'status', '--porcelain=v1', '--untracked-files=all') $Source $GitDeadline
    if ((Get-Item -LiteralPath (Join-Path $Bundle $Status.stdout)).Length -ne 0) {
        throw 'Validation checkout is dirty.'
    }

    $Definition = Get-Content -Raw -LiteralPath (Join-Path $Source $DefinitionPath) | ConvertFrom-Json
    if ($Definition.id -cne $Report.checkpoint) {
        throw "Expected checkpoint $($Report.checkpoint) in $DefinitionPath."
    }
    $Config = $Definition.$Mode
    $Report.configuration = $Config.configuration
    $Report.workItems = $Config.workItems

    if ($Mode -in @('Peers', 'BaselineBuild', 'BaselineRuntime')) {
        $EngineBuild = if ($Mode -eq 'BaselineRuntime') { $Definition.BaselineBuild } else { $Config }
        $EngineConfiguration = if ($Shipping) { 'Shipping' } else { $EngineBuild.configuration }
        $Report.configuration = $EngineConfiguration
        if ($Mode -eq 'BaselineRuntime') {
            . (Join-Path $PSScriptRoot 'W0Evidence.ps1')
            . (Join-Path $PSScriptRoot 'W0Runtime.ps1')
            $RuntimeRoot = Initialize-W0Runtime $GameDataRoot $Map $Config $Workspace
        }
        $Configure = @('-S', $Source, '-B', $Build, '-A', 'x64', '-DIXRAY_MP=ON', '-DIXRAY_MP_SQL=OFF')
        if ($Shipping) { $Configure += '-DDEVIXRAY_ENABLE_SHIPPING=ON' }
        if ($Mode -eq 'Peers') {
            $Configure += @('-DIXRAY_TESTS=ON', '-DIXRAY_MULTIPLAYER_VERIFY_TEST_FAILURE=ON')
        }
        $null = Invoke-CheckpointStep 'configure' $CMake $Configure $Source $EngineBuild.configureTimeoutSeconds
        $BuildArguments = @('--build', $Build, '--config', $EngineConfiguration, '--parallel', '--target') + $EngineBuild.targets
        $null = Invoke-CheckpointStep 'build' $CMake $BuildArguments $Source $EngineBuild.buildTimeoutSeconds
        if ($Mode -eq 'BaselineRuntime') {
            $BinaryDirectory = Join-Path $Build "bin/$EngineConfiguration"
            $Report.status = Invoke-W0Runtime $RuntimeRoot $Map $Config $RuntimeCase $BinaryDirectory $NavigationFault
        }
        elseif ($Mode -eq 'BaselineBuild') {
            $Report.status = 'build_passed'
        }
    }
    else {
        $Configure = @('-S', $Config.sourceDirectory, '-B', $Build, '-DIXRAY_MULTIPLAYER_VERIFY_TEST_FAILURE=ON', "-DCMAKE_BUILD_TYPE=$($Config.configuration)")
        if ($Config.architecture) {
            $Configure += @('-A', $Config.architecture)
        }
        $null = Invoke-CheckpointStep 'configure' $CMake $Configure $Source $Config.configureTimeoutSeconds
        $null = Invoke-CheckpointStep 'build' $CMake @('--build', $Build, '--config', $Config.configuration, '--target', $Config.target) $Source $Config.buildTimeoutSeconds
    }

    if ($Mode -in @('Portable', 'Peers')) {
        $CTestArgs = @('--test-dir', $Build, '-C', $Config.configuration, '--no-tests=error', '--output-on-failure', '--verbose')
        $Unit = $Definition.Portable
        $Tests = @(@{ name = $Unit.test; successMessage = $Unit.testSuccessMessage; step = 'test' })
        if ($Unit.PSObject.Properties['additionalTests']) {
            $Tests += $Unit.additionalTests
        }
        foreach ($Test in $Tests) {
            Invoke-AssertedCTest $CTest $CTestArgs $Source $Unit.testTimeoutSeconds $Test
        }

        $FailurePattern = '^' + [regex]::Escape($Unit.deliberateFailureTest) + '$'
        $FailureResults = Join-Path $Bundle 'deliberate-failure.xml'
        $null = Invoke-CheckpointStep 'deliberate-failure' $CTest ($CTestArgs + @('-R', $FailurePattern, '--output-junit', $FailureResults)) $Source $Unit.testTimeoutSeconds @(8)
        $FailureResult = Read-CheckpointTest $FailureResults $Unit.deliberateFailureTest
        $Failure = $FailureResult.SelectSingleNode('failure')
        $FailureOutput = $FailureResult.SelectSingleNode('system-out')
        if ($FailureResult.GetAttribute('status') -cne 'fail' -or !$Failure -or $Failure.GetAttribute('message') -cne 'Failed' -or
            !$FailureOutput -or ($FailureOutput.InnerText -split '\r?\n') -cnotcontains $Unit.deliberateFailureMessage) {
            throw 'Expected assertion did not fail. See deliberate-failure.xml.'
        }
        if ($Mode -eq 'Peers') {
            $PeerTests = @($Config.test)
            if ($Config.PSObject.Properties['additionalTests']) {
                $PeerTests += $Config.additionalTests
            }
            foreach ($Test in $PeerTests) {
                Invoke-AssertedCTest $CTest $CTestArgs $Source $Config.testTimeoutSeconds $Test
            }
            $Report.status = 'peers_passed'
        }
        else {
            $Report.status = 'portable_passed'
        }
    }
}
catch {
    $Report.error = $_.Exception.Message
    [Console]::Error.WriteLine($Report.error)
}
finally {
    $Report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding utf8
    Write-Host "W0 result $($Report.status)"
    Write-Host "W0 report $ReportPath"
}

if ($Report.status -in @('failed', 'runtime_observation_failed', 'runtime_observation_incomplete')) {
    exit 1
}
