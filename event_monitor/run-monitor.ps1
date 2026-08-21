<#
  NEED Summer Monitor launcher (Windows -> Linux DB over SSH tunnel).

  What it does:
    1. Loads .env (KEY=VALUE lines) into the process environment.
    2. Forces DB_HOST=127.0.0.1 and DB_PORT=<TUNNEL_LOCAL_PORT> (the monitor talks to the tunnel).
    3. Opens an SSH port-forward tunnel to the Linux MySQL if it is not already up.
    4. npm install (first run only) and starts the monitor.

  Usage:
    - Copy .env.example to .env and fill the ==CHANGE== values (once).
    - Right-click -> Run with PowerShell, or:  powershell -ExecutionPolicy Bypass -File run-monitor.ps1
    - Browser: http://localhost:<PORT>   (default 8787)
    - Stop the monitor with Ctrl+C. The SSH tunnel window stays open and is reused next time.
#>

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

# --- 1) load .env ---
$envPath = Join-Path $here ".env"
if (-not (Test-Path $envPath)) {
    Write-Error ".env not found. Copy .env.example to .env and fill it first."
    exit 1
}
$cfg = @{}
foreach ($raw in Get-Content $envPath) {
    $line = $raw.Trim()
    if ($line -eq "" -or $line.StartsWith("#")) { continue }
    $i = $line.IndexOf("=")
    if ($i -lt 1) { continue }
    $k = $line.Substring(0, $i).Trim()
    $v = $line.Substring($i + 1).Trim()
    $cfg[$k] = $v
    Set-Item -Path "Env:$k" -Value $v
}

# --- guard against unfilled placeholders ---
$needFilled = @("SSH_USER", "SSH_HOST", "DB_PASS", "MONITOR_PASS")
foreach ($key in $needFilled) {
    if (-not $cfg.ContainsKey($key) -or $cfg[$key] -eq "" -or $cfg[$key] -like "*==CHANGE==*") {
        Write-Error "'.env' value '$key' is not filled. Edit .env and set it."
        exit 1
    }
}

# --- 2) point the monitor at the tunnel ---
$localPort = if ($cfg.TUNNEL_LOCAL_PORT) { $cfg.TUNNEL_LOCAL_PORT } else { "3307" }
$env:DB_HOST = "127.0.0.1"
$env:DB_PORT = $localPort

# --- 3) open SSH tunnel if not already listening ---
$already = Get-NetTCPConnection -LocalPort ([int]$localPort) -State Listen -ErrorAction SilentlyContinue
if ($already) {
    Write-Host "[tunnel] 127.0.0.1:$localPort already listening - reusing existing tunnel."
}
else {
    $sshPort = if ($cfg.SSH_PORT) { $cfg.SSH_PORT } else { "22" }
    $rHost = if ($cfg.REMOTE_DB_HOST) { $cfg.REMOTE_DB_HOST } else { "127.0.0.1" }
    $rPort = if ($cfg.REMOTE_DB_PORT) { $cfg.REMOTE_DB_PORT } else { "3306" }
    $fwd = "${localPort}:${rHost}:${rPort}"
    $target = "$($cfg.SSH_USER)@$($cfg.SSH_HOST)"
    Write-Host "[tunnel] ssh -p $sshPort -L $fwd $target -N"
    Write-Host "[tunnel] (a new SSH window opens; enter your password/key passphrase there if prompted)"
    $sshArgs = @("-p", $sshPort, "-L", $fwd, $target, "-N",
        "-o", "ServerAliveInterval=30", "-o", "ExitOnForwardFailure=yes")
    Start-Process -FilePath "ssh" -ArgumentList $sshArgs
    # wait up to ~10s for the local port to come up
    for ($t = 0; $t -lt 20; $t++) {
        Start-Sleep -Milliseconds 500
        if (Get-NetTCPConnection -LocalPort ([int]$localPort) -State Listen -ErrorAction SilentlyContinue) { break }
    }
    if (Get-NetTCPConnection -LocalPort ([int]$localPort) -State Listen -ErrorAction SilentlyContinue) {
        Write-Host "[tunnel] up on 127.0.0.1:$localPort"
    }
    else {
        Write-Warning "[tunnel] port $localPort is not up yet. Check the SSH window (password/key prompt or host key confirmation?), then re-run."
    }
}

# --- 4) deps (first run only) ---
if (-not (Test-Path (Join-Path $here "node_modules"))) {
    Write-Host "[deps] npm install ..."
    npm install --no-audit --no-fund
}

# --- 5) start the monitor ---
$port = if ($env:PORT) { $env:PORT } else { "8787" }
Write-Host "[monitor] starting -> http://localhost:$port   (Ctrl+C to stop)"
node src/server.js
