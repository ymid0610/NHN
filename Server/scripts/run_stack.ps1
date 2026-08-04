<#
.SYNOPSIS
    Starts the four servers, waits for them to register with each other, and
    leaves them running.

.DESCRIPTION
    The match server must come up first: the other three dial it, and they
    announce their own endpoints rather than being configured into it.

    Servers are started with --no-wait so a fatal error prints its reason and
    exits instead of blocking on an acknowledgement nobody is there to give.
    Drop that switch when debugging interactively.

.EXAMPLE
    .\run_stack.ps1
    .\run_stack.ps1 -Config release
    .\run_stack.ps1 -Stop
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug',

    [switch]$Stop,

    [string]$LogLevel = 'info'
)

$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $serverRoot "build\$Config\bin"
$logDir = Join-Path $serverRoot "logs"

$servers = @('MatchServer', 'ChatServer', 'VoiceServer', 'InstanceServer')

if ($Stop) {
    foreach ($name in $servers) {
        Get-Process -Name $name -ErrorAction SilentlyContinue | Stop-Process -Force
    }
    Write-Host "stopped." -ForegroundColor Yellow
    return
}

if (-not (Test-Path $binDir)) {
    throw "no build at $binDir - run: cmake --preset $Config; cmake --build --preset $Config"
}

if (-not (Test-Path $logDir)) {
    New-Item -ItemType Directory -Path $logDir | Out-Null
}

# Anything left over from a previous run would hold the listening ports.
foreach ($name in $servers) {
    Get-Process -Name $name -ErrorAction SilentlyContinue | Stop-Process -Force
}

foreach ($name in $servers) {
    $exe = Join-Path $binDir "$name.exe"
    if (-not (Test-Path $exe)) {
        throw "missing $exe"
    }

    Start-Process -FilePath $exe `
        -ArgumentList "--no-wait", "--log-level=$LogLevel", "--log-dir=$logDir" `
        -WorkingDirectory $binDir `
        -RedirectStandardOutput (Join-Path $logDir "$name.out.txt") `
        -RedirectStandardError (Join-Path $logDir "$name.err.txt") `
        -WindowStyle Hidden | Out-Null

    Write-Host "started $name" -ForegroundColor Green

    # The match server has to be listening before the others dial it.
    if ($name -eq 'MatchServer') {
        Start-Sleep -Milliseconds 700
    }
}

# Let the peer links register before anyone tries to connect: a client that
# arrives first simply gets no chat or voice endpoint.
Start-Sleep -Seconds 2

$matchLog = Join-Path $logDir "MatchServer.out.txt"
$registered = (Select-String -Path $matchLog -Pattern 'peer registered' -ErrorAction SilentlyContinue).Count
Write-Host "`n$registered of 3 peers registered with the match server" -ForegroundColor Cyan
Write-Host "logs: $logDir"
Write-Host "stop with: .\run_stack.ps1 -Stop"
