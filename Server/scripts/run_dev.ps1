<#
.SYNOPSIS
    Starts the four servers and opens interactive test clients, each in its own
    console window.

.DESCRIPTION
    The everyday driver: one command gets you a running stack and two clients
    you can actually type into.

    Clients are opened in real console windows rather than with redirected
    output. Redirecting would break the prompt, and it would also silently
    disable the wait-for-developer step on a crash, which only happens when
    stdin is a console. For the same reason they are NOT given --no-wait: a
    fault while you are sitting there is exactly when you want it to stop and
    show you the stack.

    Each client gets a distinct nickname so room rosters are readable.

.PARAMETER Clients
    How many client windows to open. Default 2.

.PARAMETER NoServers
    Attach to an already-running stack instead of restarting it.

.EXAMPLE
    .\run_dev.ps1
    .\run_dev.ps1 -Clients 4
    .\run_dev.ps1 -NoServers -Clients 1
    .\run_dev.ps1 -Stop
#>
[CmdletBinding()]
param(
    [ValidateRange(0, 8)]
    [int]$Clients = 2,

    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug',

    [switch]$NoServers,

    [switch]$Stop
)

$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $serverRoot "build\$Config\bin"
$stackScript = Join-Path $PSScriptRoot 'run_stack.ps1'

if ($Stop) {
    & $stackScript -Stop -Config $Config
    Get-Process -Name TestClient -ErrorAction SilentlyContinue | Stop-Process -Force
    Write-Host "stopped servers and clients." -ForegroundColor Yellow
    return
}

$exe = Join-Path $binDir 'TestClient.exe'
if (-not (Test-Path $exe)) {
    throw "missing $exe - run: cmake --preset $Config; cmake --build --preset $Config"
}

if (-not $NoServers) {
    & $stackScript -Config $Config
}

# Nicknames are cosmetic and non-unique server-side, but distinct ones make
# 'room info' and kick-by-nick usable.
$names = @('alice', 'bob', 'carol', 'dave', 'erin', 'frank', 'grace', 'heidi')

for ($i = 0; $i -lt $Clients; $i++) {
    $nick = $names[$i]

    # -WindowStyle Normal and no redirection: these are meant to be typed into.
    Start-Process -FilePath $exe `
        -ArgumentList "--nick=$nick", "--log-level=warn" `
        -WorkingDirectory $binDir `
        -WindowStyle Normal | Out-Null

    Write-Host "opened client '$nick'" -ForegroundColor Green
    Start-Sleep -Milliseconds 250
}

if ($Clients -gt 0) {
    Write-Host @"

In each client window:
  connect                       connect and say hello
  room create quad arena        (first window)
  room joinname arena           (the others)
  room ready on                 everyone except the host
  room start                    host, once the rest are ready
  chat room hi / chat global hi
  help                          everything else

Stop everything with: .\run_dev.ps1 -Stop
"@ -ForegroundColor Cyan
}
