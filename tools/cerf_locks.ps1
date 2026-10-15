function New-CerfLock {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int]$StaleSeconds,
        [Parameter(Mandatory = $true)][string]$Label,
        [int]$PollSeconds = 10,
        [int]$HeartbeatSeconds = 15
    )
    [pscustomobject]@{
        Path             = $Path
        StaleSeconds     = $StaleSeconds
        Label            = $Label
        PollSeconds      = $PollSeconds
        HeartbeatSeconds = $HeartbeatSeconds
        Held             = $false
        Heartbeat        = $null
    }
}

function New-CerfRunLocks {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Label
    )
    foreach ($slot in 1..4) {
        New-CerfLock -Path (Join-Path $Root ".cerf_lock.$slot") -StaleSeconds 120 -Label $Label
    }
}

function Read-CerfLockState {
    param([Parameter(Mandatory = $true)]$Lock)
    if (-not (Test-Path $Lock.Path)) { return $null }
    $lines = $null
    try { $lines = [IO.File]::ReadAllLines($Lock.Path) } catch { return $null }
    if (-not $lines -or $lines.Count -lt 1) { return $null }
    $stamp = $null
    try {
        $stamp = [datetime]::Parse($lines[0], [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
    } catch {
        return $null
    }
    $ownerPid = 0
    if ($lines.Count -ge 2) { [void][int]::TryParse($lines[1], [ref]$ownerPid) }
    [pscustomobject]@{
        StampUtc   = $stamp.ToUniversalTime()
        OwnerPid   = $ownerPid
        AgeSeconds = [int]((Get-Date).ToUniversalTime() - $stamp.ToUniversalTime()).TotalSeconds
    }
}

function Update-CerfLockStamp {
    param([Parameter(Mandatory = $true)]$Lock)
    if (-not $Lock.Held) { return }
    for ($i = 0; $i -lt 5; $i++) {
        try {
            [IO.File]::WriteAllLines($Lock.Path, [string[]]@((Get-Date).ToUniversalTime().ToString("o"), "$PID"))
            return
        } catch {
            Start-Sleep -Milliseconds 200
        }
    }
}

function Start-CerfLockHeartbeat {
    param([Parameter(Mandatory = $true)]$Lock)
    $child = "`$lock = '$($Lock.Path)'; " +
             "while (`$true) { " +
             "Start-Sleep -Seconds $($Lock.HeartbeatSeconds); " +
             "if (-not (Get-Process -Id $PID -ErrorAction SilentlyContinue)) { break }; " +
             "if (-not (Test-Path `$lock)) { break }; " +
             "`$lines = `$null; " +
             "try { `$lines = [IO.File]::ReadAllLines(`$lock) } catch { }; " +
             "if (`$lines -and `$lines.Count -ge 2) { " +
             "if (`$lines[1] -ne '$PID') { break }; " +
             "try { [IO.File]::WriteAllLines(`$lock, [string[]]@((Get-Date).ToUniversalTime().ToString('o'), '$PID')) } catch { } } }"
    $Lock.Heartbeat = Start-Process -FilePath "powershell" `
        -ArgumentList @("-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", $child) `
        -NoNewWindow -PassThru
}

function Get-CerfLockHolder {
    param([Parameter(Mandatory = $true)]$Lock)
    if (-not (Test-Path $Lock.Path)) { return $null }
    $state = Read-CerfLockState $Lock
    if ($state -and $state.AgeSeconds -lt $Lock.StaleSeconds) { return $state }
    if ($state) {
        Write-Host "[$($Lock.Label)] removing stale $(Split-Path -Leaf $Lock.Path) (owner PID $($state.OwnerPid), refreshed $($state.AgeSeconds) s ago, stale at $($Lock.StaleSeconds) s)"
    } else {
        Write-Host "[$($Lock.Label)] removing unreadable $(Split-Path -Leaf $Lock.Path)"
    }
    Remove-Item $Lock.Path -Force -ErrorAction SilentlyContinue
    if (Test-Path $Lock.Path) {
        return [pscustomobject]@{ StampUtc = (Get-Date).ToUniversalTime(); OwnerPid = 0; AgeSeconds = 0 }
    }
    return $null
}

function Wait-CerfLock {
    param([Parameter(Mandatory = $true)]$Lock)
    while ($true) {
        $holder = Get-CerfLockHolder $Lock
        if (-not $holder) { return }
        Write-Host "[$($Lock.Label)] waiting for $(Split-Path -Leaf $Lock.Path) held by PID $($holder.OwnerPid) (refreshed $($holder.AgeSeconds) s ago, stale at $($Lock.StaleSeconds) s)..."
        Start-Sleep -Seconds $Lock.PollSeconds
    }
}

function Enter-CerfLockNoWait {
    param([Parameter(Mandatory = $true)]$Lock)
    try {
        $stream = [IO.File]::Open($Lock.Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $stream.Close()
    } catch [IO.IOException] {
        return $false
    }
    $Lock.Held = $true
    Update-CerfLockStamp $Lock
    Start-CerfLockHeartbeat $Lock
    return $true
}

function Enter-CerfLock {
    param([Parameter(Mandatory = $true)]$Lock)
    while (-not (Enter-CerfLockNoWait $Lock)) {
        $holder = Get-CerfLockHolder $Lock
        if ($holder) {
            Write-Host "[$($Lock.Label)] waiting for $(Split-Path -Leaf $Lock.Path) held by PID $($holder.OwnerPid) (refreshed $($holder.AgeSeconds) s ago, stale at $($Lock.StaleSeconds) s)..."
            Start-Sleep -Seconds $Lock.PollSeconds
        }
    }
}

function Enter-CerfLockSlot {
    param([Parameter(Mandatory = $true)][object[]]$Locks)
    while ($true) {
        foreach ($l in $Locks) {
            if (Enter-CerfLockNoWait $l) { return $l }
        }
        $held = @()
        foreach ($l in $Locks) {
            $holder = Get-CerfLockHolder $l
            if ($holder) { $held += "$(Split-Path -Leaf $l.Path) PID $($holder.OwnerPid)" }
        }
        if ($held.Count -lt $Locks.Count) { continue }
        Write-Host "[$($Locks[0].Label)] waiting for a free run slot, all $($Locks.Count) are held: $($held -join ', ')..."
        Start-Sleep -Seconds $Locks[0].PollSeconds
    }
}

function Exit-CerfLock {
    param([Parameter(Mandatory = $true)]$Lock)
    if ($Lock.Heartbeat) {
        Stop-Process -Id $Lock.Heartbeat.Id -Force -ErrorAction SilentlyContinue
        $Lock.Heartbeat = $null
    }
    if (-not $Lock.Held) { return }
    $Lock.Held = $false
    $state = Read-CerfLockState $Lock
    if ($state -and $state.OwnerPid -ne $PID) { return }
    Remove-Item $Lock.Path -Force -ErrorAction SilentlyContinue
}

function Exit-CerfLockSlot {
    param([Parameter(Mandatory = $true)][object[]]$Locks)
    foreach ($l in $Locks) { Exit-CerfLock $l }
}
