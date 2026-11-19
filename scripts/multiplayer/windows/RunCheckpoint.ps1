#Requires -Version 7.4

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string] $Revision,

    [ValidateSet('Portable', 'BaselineBuild')]
    [string] $Mode = 'Portable',

    [string] $ResultsRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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

$ControllerRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$Revision = $Revision.ToLowerInvariant()
if (!$ResultsRoot) {
    $ResultsRoot = Join-Path $ControllerRoot 'build/test-results/multiplayer'
}
$RunId = (Get-Date -AsUTC -Format 'yyyyMMddTHHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$Bundle = (New-Item -ItemType Directory -Path (Join-Path $ResultsRoot "$Revision/$RunId/logs") -Force).Parent.FullName
$Source = (New-Item -ItemType Directory -Path (Join-Path $Bundle 'source')).FullName
$ReportPath = Join-Path $Bundle 'report.json'
$Report = [ordered] @{
    checkpoint = 'W0'
    mode = $Mode
    requestedRevision = $Revision
    actualRevision = $null
    configuration = $null
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
    if ($Mode -eq 'Portable') {
        $CTest = (Get-Command ctest -CommandType Application | Select-Object -First 1).Source
    }

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

    if ($Mode -eq 'BaselineBuild') {
        $null = Invoke-CheckpointStep 'configure' $CMake @('--preset', $Config.configurePreset, '-DIXRAY_MP_SQL=OFF') $Source $Config.configureTimeoutSeconds
        $null = Invoke-CheckpointStep 'build' $CMake (@('--build', '--preset', $Config.buildPreset, '--target') + $Config.targets) $Source $Config.buildTimeoutSeconds
        $Report.status = 'build_passed'
    }
    else {
        $Build = Join-Path $Bundle 'portable-build'
        $Configure = @('-S', $Config.sourceDirectory, '-B', $Build, '-G', $Config.generator, '-DIXRAY_MULTIPLAYER_VERIFY_TEST_FAILURE=ON', "-DCMAKE_BUILD_TYPE=$($Config.configuration)")
        if ($Config.architecture) {
            $Configure += @('-A', $Config.architecture)
        }
        $null = Invoke-CheckpointStep 'configure' $CMake $Configure $Source $Config.configureTimeoutSeconds
        $null = Invoke-CheckpointStep 'build' $CMake @('--build', $Build, '--config', $Config.configuration, '--target', $Config.target) $Source $Config.buildTimeoutSeconds
        $CTestArgs = @('--test-dir', $Build, '-C', $Config.configuration, '--no-tests=error', '--output-on-failure', '--verbose')
        $TestPattern = '^' + [regex]::Escape($Config.test) + '$'
        $FailurePattern = '^' + [regex]::Escape($Config.deliberateFailureTest) + '$'
        $TestResults = Join-Path $Bundle 'test.xml'
        $null = Invoke-CheckpointStep 'test' $CTest ($CTestArgs + @('-R', $TestPattern, '--output-junit', $TestResults)) $Source $Config.testTimeoutSeconds
        $TestResult = Read-CheckpointTest $TestResults $Config.test
        if ($TestResult.GetAttribute('status') -cne 'run' -or !$TestResult.SelectSingleNode('system-out') -or
            ($TestResult.SelectSingleNode('system-out').InnerText -split '\r?\n') -cnotcontains $Config.testSuccessMessage) {
            throw 'Sequence test did not report completion.'
        }

        $FailureResults = Join-Path $Bundle 'deliberate-failure.xml'
        $null = Invoke-CheckpointStep 'deliberate-failure' $CTest ($CTestArgs + @('-R', $FailurePattern, '--output-junit', $FailureResults)) $Source $Config.testTimeoutSeconds @(8)
        $FailureResult = Read-CheckpointTest $FailureResults $Config.deliberateFailureTest
        $Failure = $FailureResult.SelectSingleNode('failure')
        $FailureOutput = $FailureResult.SelectSingleNode('system-out')
        if ($FailureResult.GetAttribute('status') -cne 'fail' -or !$Failure -or $Failure.GetAttribute('message') -cne 'Failed' -or
            !$FailureOutput -or ($FailureOutput.InnerText -split '\r?\n') -cnotcontains $Config.deliberateFailureMessage) {
            throw 'Expected assertion did not fail. See deliberate-failure.xml.'
        }
        $Report.status = 'portable_passed'
    }
}
catch {
    $Report.error = $_.Exception.Message
    [Console]::Error.WriteLine($Report.error)
}
finally {
    $Report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding utf8
    Write-Host "W0 report $ReportPath"
}

if ($Report.status -eq 'failed') {
    exit 1
}
