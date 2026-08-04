<#
.SYNOPSIS
    Four-player end-to-end scenario: one host plus three joiners, each its own
    process driven by a script.

.DESCRIPTION
    Requires the stack to already be running (see run_stack.ps1).

    Joiner output goes to files; the host runs in the foreground so its
    transcript is what you read. The exit criterion is the host reaching the
    instance, which only happens if room creation, joining, readiness, the
    handoff and the instance handshake all worked.

.EXAMPLE
    .\run_stack.ps1
    .\run_quad_test.ps1
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug'
)

$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $serverRoot "build\$Config\bin"
$exe = Join-Path $binDir 'TestClient.exe'
$outDir = Join-Path $serverRoot 'logs'

if (-not (Test-Path $exe)) {
    throw "missing $exe"
}
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$hostScript = Join-Path $PSScriptRoot 'smoke_host.txt'
$joinScript = Join-Path $PSScriptRoot 'smoke_join.txt'

# The host goes first so the room exists before the joiners search for it.
$hostProcess = Start-Process -FilePath $exe `
    -ArgumentList "--no-wait", "--log-level=warn", "--nick=host", "--script=$hostScript" `
    -WorkingDirectory $binDir `
    -RedirectStandardOutput (Join-Path $outDir 'client_host.txt') `
    -RedirectStandardError (Join-Path $outDir 'client_host.err.txt') `
    -PassThru -WindowStyle Hidden

Start-Sleep -Milliseconds 400

$joiners = @()
foreach ($nick in @('p2', 'p3', 'p4')) {
    $joiners += Start-Process -FilePath $exe `
        -ArgumentList "--no-wait", "--log-level=warn", "--nick=$nick", "--script=$joinScript" `
        -WorkingDirectory $binDir `
        -RedirectStandardOutput (Join-Path $outDir "client_$nick.txt") `
        -RedirectStandardError (Join-Path $outDir "client_$nick.err.txt") `
        -PassThru -WindowStyle Hidden
}

Write-Host "running four clients..." -ForegroundColor Cyan

$all = @($hostProcess) + $joiners
foreach ($process in $all) {
    if (-not $process.WaitForExit(90000)) {
        Write-Warning "client $($process.Id) did not finish; killing"
        $process.Kill()
    }
}

Write-Host "`n===== host transcript =====" -ForegroundColor Cyan
Get-Content (Join-Path $outDir 'client_host.txt')

Write-Host "`n===== p2 (tail) =====" -ForegroundColor Cyan
Get-Content (Join-Path $outDir 'client_p2.txt') | Select-Object -Last 20
