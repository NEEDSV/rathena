# G2 isolated server control. Touches ONLY processes whose executable path is inside <out>\stack.
# usage: bc_servers.ps1 -Out <scratch>\mbqa_bc -Action start|startmap|killmap|killchar|startchar|stopall|status
param([Parameter(Mandatory)] [string]$Out, [Parameter(Mandatory)] [string]$Action, [switch]$TestHooks)
$ErrorActionPreference = 'Stop'
$stack = Join-Path $Out 'stack'
$logs = Join-Path $Out 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$pidFile = Join-Path $Out 'pids.json'
$ports = @{ 'login-server' = 26901; 'char-server' = 26121; 'map-server' = 25121 }

function Get-Pids { if (Test-Path $pidFile) { Get-Content $pidFile -Raw | ConvertFrom-Json } else { [pscustomobject]@{} } }
function Save-Pid($name, $id) { $p = Get-Pids; $p | Add-Member -NotePropertyName $name -NotePropertyValue $id -Force; $p | ConvertTo-Json | Set-Content -Encoding ascii $pidFile }

function Owned($name) {
    $p = Get-Pids
    $id = $p.$name
    if (-not $id) { return $null }
    $proc = Get-CimInstance Win32_Process -Filter "ProcessId=$id" -ErrorAction SilentlyContinue
    if (-not $proc) { return $null }
    $exe = [IO.Path]::GetFullPath($proc.ExecutablePath)
    $want = [IO.Path]::GetFullPath((Join-Path $stack "$name.exe"))
    if ($exe -ne $want) { throw "PID $id is $exe, not $want - refusing" }
    return $proc
}

function Preflight {
    $s = Get-Content (Join-Path $Out 'secrets.json') -Raw | ConvertFrom-Json
    $env:MBQA_RT_PW = $s.rt
    $env:NEED_WIKI_PORT = '26906'
    $r = & python -I 'E:\tools\Need\rathena\tools\need_marble\isolation_preflight.py' --approved (Join-Path $Out 'approved.json') --check-db 2>&1
    $r | Out-File -Encoding utf8 (Join-Path $logs ("preflight_{0}.txt" -f (Get-Date -Format 'HHmmss')))
    $env:MBQA_RT_PW = ''
    return ($LASTEXITCODE -eq 0)
}

function Start-One($name) {
    if (Owned $name) { throw "$name already running" }
    $busy = Get-NetTCPConnection -State Listen -LocalPort $ports[$name] -ErrorAction SilentlyContinue
    if ($busy) { throw "port $($ports[$name]) busy" }
    $env:NEED_WIKI_PORT = '26906'
    if ($TestHooks -and $name -eq 'map-server') { $env:NEED_MARBLE_TEST_HOOKS = '1' } else { $env:NEED_MARBLE_TEST_HOOKS = '' }
    $stamp = Get-Date -Format 'HHmmss'
    $p = Start-Process -FilePath (Join-Path $stack "$name.exe") -WorkingDirectory $stack -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logs "$name.$stamp.out") -RedirectStandardError (Join-Path $logs "$name.$stamp.err")
    Save-Pid $name $p.Id
    "$name started pid=$($p.Id)"
}

function Wait-Map {
    $log = Join-Path $stack 'log\map-msg_log.log'
    $start = if (Test-Path $log) { (Get-Item $log).Length } else { 0 }
    for ($i = 0; $i -lt 300; $i++) {
        Start-Sleep -Seconds 2
        if (Test-Path $log) {
            $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
            $fs.Seek($start, 'Begin') | Out-Null
            $t = (New-Object IO.StreamReader($fs)).ReadToEnd(); $fs.Close()
            if ($t -match 'NEED_MARBLE v2: board (ok|check failed)[^\r\n]*') { Start-Sleep -Seconds 8; return "map ready after $(2*$i+10)s: $($Matches[0])" }
        }
        if (-not (Owned 'map-server')) { return 'map-server exited during load' }
    }
    return 'map-server not ready in 600s'
}

function Kill-One($name) {
    $proc = Owned $name
    if (-not $proc) { return "$name not running" }
    Stop-Process -Id $proc.ProcessId -Force
    Start-Sleep -Seconds 1
    "$name killed pid=$($proc.ProcessId)"
}

switch ($Action) {
    'start' {
        if (-not (Preflight)) { 'PREFLIGHT FAIL - DO NOT START'; exit 1 }
        Start-One 'login-server'; Start-Sleep 2
        Start-One 'char-server'; Start-Sleep 3
        Start-One 'map-server'
        Wait-Map
    }
    'startmap' { Start-One 'map-server'; Wait-Map }
    'startchar' { Start-One 'char-server'; Start-Sleep 3 }
    'killmap' { Kill-One 'map-server' }
    'killchar' { Kill-One 'char-server' }
    'stopall' { Kill-One 'map-server'; Kill-One 'char-server'; Kill-One 'login-server' }
    'status' {
        foreach ($n in 'login-server', 'char-server', 'map-server') { $p = Owned $n; "{0} {1}" -f $n, $(if ($p) { "pid=$($p.ProcessId)" } else { 'down' }) }
        Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue | Where-Object { $_.LocalPort -in 26901, 26121, 25121, 26906, 3327 } |
            ForEach-Object { "{0}:{1} pid={2}" -f $_.LocalAddress, $_.LocalPort, $_.OwningProcess }
    }
}
