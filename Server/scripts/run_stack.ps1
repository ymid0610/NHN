<#
.SYNOPSIS
    Starts the four servers, waits for them to register with each other, and
    leaves them running.

.DESCRIPTION
    The match server must come up first: the other three dial it, and they
    announce their own endpoints rather than being configured into it.

    Each server gets its own console window. That is not cosmetic. Start-Process
    can only redirect output by going through CreateProcess *without*
    CREATE_NEW_CONSOLE, which makes the children inherit the launching console
    and join its process group (-WindowStyle Hidden is quietly ignored on that
    path). When that console then goes away - which is exactly what happens if
    you double-click this script or use "Run with PowerShell", since the window
    closes the moment the script returns - Windows delivers CTRL_CLOSE_EVENT to
    every attached process, and the servers shut down as instructed.

    So: no redirection, own console, survives the launcher. Nothing is lost,
    because each server already writes logs/<Name>_<timestamp>.log itself.

    In this mode the servers are NOT given --no-wait: a fatal error stops in
    that server's own window with the reason and stack on screen, which is what
    you want while developing.

    -Hidden restores the old behaviour for automation, where the caller stays
    alive for the duration and wants the output captured to files.

.EXAMPLE
    .\run_stack.ps1
    .\run_stack.ps1 -Config release
    .\run_stack.ps1 -Hidden          # for scripted runs; dies with the caller
    .\run_stack.ps1 -Stop
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug',

    [switch]$Stop,

    # Run without windows and capture output to files. Only safe when the
    # launching console outlives the servers.
    [switch]$Hidden,

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

    if ($Hidden) {
        # Redirection forces console inheritance; only use this when the caller
        # sticks around, or the servers die with it.
        Start-Process -FilePath $exe `
            -ArgumentList "--no-wait", "--log-level=$LogLevel", "--log-dir=$logDir" `
            -WorkingDirectory $binDir `
            -RedirectStandardOutput (Join-Path $logDir "$name.out.txt") `
            -RedirectStandardError (Join-Path $logDir "$name.err.txt") `
            -WindowStyle Hidden | Out-Null
    }
    else {
        # No redirection, so Start-Process goes through ShellExecute and the
        # server gets its own console - detached from this one's process group.
        Start-Process -FilePath $exe `
            -ArgumentList "--log-level=$LogLevel", "--log-dir=$logDir" `
            -WorkingDirectory $binDir `
            -WindowStyle Minimized | Out-Null
    }

    Write-Host "started $name" -ForegroundColor Green

    # The match server has to be listening before the others dial it.
    if ($name -eq 'MatchServer') {
        Start-Sleep -Milliseconds 700
    }
}

# Read the server's own log rather than captured stdout, so this works the same
# in both modes.
#
# Sorted by name, not LastWriteTime: the filename carries a sortable timestamp,
# whereas Windows defers the directory-entry timestamp of a file that is still
# open, which would pick a stale log from an earlier run.
$matchLog = Get-ChildItem (Join-Path $logDir 'MatchServer_*.log') -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | Select-Object -First 1

# Poll rather than sleeping a fixed amount: registration normally lands in well
# under a second, but a cold start after a build can take longer, and reporting
# 0 of 3 when everything is fine is worse than waiting.
# Opened with FileShare.ReadWrite. Select-String and Get-Content request a share
# mode that excludes the server's own write handle, so they fail with a sharing
# violation for exactly as long as the server is running.
function Read-LiveLog([string]$Path) {
    try {
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            $reader = New-Object System.IO.StreamReader($stream)
            return $reader.ReadToEnd()
        }
        finally { $stream.Dispose() }
    }
    catch { return '' }
}

$registered = 0
$deadline = (Get-Date).AddSeconds(10)
while ((Get-Date) -lt $deadline) {
    if (-not $matchLog) {
        $matchLog = Get-ChildItem (Join-Path $logDir 'MatchServer_*.log') -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
    }
    if ($matchLog) {
        $registered = ([regex]::Matches((Read-LiveLog $matchLog.FullName), 'peer registered')).Count
        if ($registered -ge 3) { break }
    }
    Start-Sleep -Milliseconds 250
}

$colour = if ($registered -eq 3) { 'Cyan' } else { 'Yellow' }
Write-Host "`n$registered of 3 peers registered with the match server" -ForegroundColor $colour

$alive = @(Get-Process -Name $servers -ErrorAction SilentlyContinue).Count
if ($alive -lt 4) {
    Write-Warning "only $alive of 4 servers are still running - check $logDir"
}

Write-Host "logs: $logDir"
Write-Host "stop with: .\run_stack.ps1 -Stop"
