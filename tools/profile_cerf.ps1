#requires -Version 5.1
<#
.SYNOPSIS
  CPU-profile cerf.exe with ETW sampling and extract top hotspots.

.DESCRIPTION
  Starts a Windows Performance Recorder CPU trace, launches cerf.exe with
  the given arguments, samples for the requested duration, stops the
  trace, then uses xperf to produce a symbolicated hotspot report
  scoped to the cerf process.

  Requires elevation -- ETW kernel sampling is admin-only.

.PARAMETER DurationSec
  How long to sample after cerf launches. Default 60s.

.PARAMETER CerfArgs
  Arguments forwarded to cerf.exe (e.g. "--device=wm5"). Quote the whole
  string if it contains spaces.

.PARAMETER Config
  Build config folder under build\. Default Release.

.PARAMETER OutDir
  Where the .etl + reports land. Default: tmp\profile_<timestamp>\.

.PARAMETER KeepRunning
  Leave cerf running after sampling stops. Default: kill it.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\profile_cerf.ps1
  powershell -ExecutionPolicy Bypass -File tools\profile_cerf.ps1 -CerfArgs "--device=wm5" -DurationSec 90

.NOTES
  Output files:
    cerf.etl          -- raw trace, open with wpa.exe for drill-down
    profile.txt       -- per-function CPU time summary (top-N)
    butterfly.txt     -- caller/callee view for the hottest symbols
    collection.log    -- wpr/xperf diagnostic output
#>

param(
    [int]$DurationSec = 60,
    [string]$CerfArgs = "",
    [string]$Config = "Release",
    [string]$OutDir = "",
    [switch]$KeepRunning,
    [int]$MinHits = 50,
    [switch]$SkipStaleCheck
)

# Use "Continue" so PS doesn't treat native-command stderr output as a
# terminating error. We check $LASTEXITCODE explicitly after each native
# call (wpr/xperf) and throw on real failures.
$ErrorActionPreference = "Continue"

function Require-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $pr = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host ""
        Write-Host "ERROR: profile_cerf.ps1 must run elevated." -ForegroundColor Red
        Write-Host "  ETW CPU sampling uses the kernel profiler provider, which is admin-only."
        Write-Host ""
        Write-Host "  Fix: open a PowerShell window as Administrator, then:"
        Write-Host "    cd <path-to-cerf-repo>"
        Write-Host "    powershell -ExecutionPolicy Bypass -File tools\profile_cerf.ps1 $($MyInvocation.UnboundArguments -join ' ')"
        Write-Host ""
        exit 2
    }
}

Require-Admin

# Resolve repo root relative to this script (tools\profile_cerf.ps1 -> ..).
# Works whether the repo is on Z:\ (VM) or any other path on the host.
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..") | Select-Object -ExpandProperty Path
Set-Location $repoRoot

$exe    = Join-Path $repoRoot "build\$Config\Win32\cerf.exe"
$pdbDir = Join-Path $repoRoot "build\$Config\Win32"
if (-not (Test-Path $exe)) {
    Write-Error "cerf.exe not found at $exe. Build first: powershell -ExecutionPolicy Bypass -File build.ps1"
    exit 1
}

# Refuse to profile a stale binary: if any cerf/**/*.cpp or .h is newer than
# the exe, the user is about to measure code that isn't in the binary. This
# turned a previous profile run into "baseline numbers dressed up as TLB
# numbers" — wasted a full sample + symbol-resolution cycle before we noticed.
$exeTime = (Get-Item $exe).LastWriteTime
$cerfDir = Join-Path $repoRoot "cerf"
$newer   = Get-ChildItem -Path $cerfDir -Recurse -Include *.cpp,*.h `
              -ErrorAction SilentlyContinue |
           Where-Object { $_.LastWriteTime -gt $exeTime } |
           Select-Object -First 5
if ($newer) {
    Write-Host ""
    Write-Host "ERROR: cerf.exe is older than source files." -ForegroundColor Red
    Write-Host "  exe:    $exe  ($exeTime)"
    Write-Host "  newer sources (first 5):"
    $newer | ForEach-Object { Write-Host ("    {0}  ({1})" -f $_.FullName, $_.LastWriteTime) }
    Write-Host ""
    Write-Host "  Build first: powershell -ExecutionPolicy Bypass -File build.ps1"
    Write-Host "  Or pass -SkipStaleCheck to profile the current exe anyway."
    Write-Host ""
    if (-not $SkipStaleCheck) { exit 3 }
}

if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $stamp  = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutDir = Join-Path $repoRoot "tmp\profile_$stamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$etl        = Join-Path $OutDir "cerf.etl"
$profileTxt = Join-Path $OutDir "profile.txt"
$butterfly  = Join-Path $OutDir "butterfly.txt"
$log        = Join-Path $OutDir "collection.log"

# Symbol path: local PDBs first, public Microsoft symbols for system DLLs.
$symCache = Join-Path $env:TEMP "cerf_symcache"
New-Item -ItemType Directory -Force -Path $symCache | Out-Null
$env:_NT_SYMBOL_PATH   = "$pdbDir;SRV*$symCache*https://msdl.microsoft.com/download/symbols"
$env:_NT_SYMCACHE_PATH = $symCache

# Use the full Windows Performance Toolkit wpr.exe (NOT the System32 shim).
# The shim lacks profiles (0x80070032) and the legacy NT Kernel Logger used by
# `xperf -on` is blocked on machines with HVCI / VBS enabled (error 0x32). The
# WPT wpr.exe uses the modern system-trace session mode which works under VBS.
$wptDir  = "C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit"
$wprExe  = Join-Path $wptDir "wpr.exe"
$xperfExe = Join-Path $wptDir "xperf.exe"
if (-not (Test-Path $wprExe)) {
    Write-Error "WPT wpr.exe not found at $wprExe. Install the Windows Performance Toolkit (Windows SDK)."
    exit 1
}
if (-not (Test-Path $xperfExe)) {
    Write-Error "WPT xperf.exe not found at $xperfExe. Install the Windows Performance Toolkit."
    exit 1
}

# Clean any leftover session from a previous aborted run.
try { & cmd.exe /c "`"$wprExe`" -cancel >nul 2>&1" | Out-Null } catch {}

"=== profile_cerf.ps1 ===" | Out-File $log -Encoding utf8
"When:       $(Get-Date -Format o)" | Out-File $log -Append -Encoding utf8
"Exe:        $exe"                 | Out-File $log -Append -Encoding utf8
"CerfArgs:   $CerfArgs"            | Out-File $log -Append -Encoding utf8
"Duration:   ${DurationSec}s"      | Out-File $log -Append -Encoding utf8
"OutDir:     $OutDir"              | Out-File $log -Append -Encoding utf8
"SymPath:    $env:_NT_SYMBOL_PATH" | Out-File $log -Append -Encoding utf8
""                                  | Out-File $log -Append -Encoding utf8

Write-Host "[PROF] Starting CPU sampling (WPT wpr, CPU profile, system-trace mode)..."
# WPT wpr -start CPU turns on sampled CPU usage + stackwalks via the modern
# system-trace session (works under HVCI/VBS, unlike legacy NT Kernel Logger).
$wprStart = & $wprExe -start CPU 2>&1
$wprStart | Out-File $log -Append -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    Write-Error "wpr -start CPU failed. See $log"
    exit 1
}

$proc   = $null
$cerfPid = $null
try {
    Write-Host "[PROF] Launching cerf: $exe $CerfArgs"
    $splat = @{ FilePath = $exe; PassThru = $true; WorkingDirectory = (Split-Path $exe) }
    if (-not [string]::IsNullOrWhiteSpace($CerfArgs)) {
        $splat.ArgumentList = $CerfArgs
    }
    $proc = Start-Process @splat
    $cerfPid = $proc.Id
    Write-Host "[PROF] cerf PID=$cerfPid. Sampling ${DurationSec}s..."

    $deadline = (Get-Date).AddSeconds($DurationSec)
    while ((Get-Date) -lt $deadline -and -not $proc.HasExited) {
        Start-Sleep -Milliseconds 500
    }

    if ($proc.HasExited) {
        Write-Host "[PROF] cerf exited on its own before deadline (exit=$($proc.ExitCode))."
    }

    Write-Host "[PROF] Stopping trace (writing $etl)..."
    $wprStop = & $wprExe -stop $etl 2>&1
    $wprStop | Out-File $log -Append -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        Write-Error "wpr -stop failed. See $log"
        exit 1
    }

    if (-not $KeepRunning -and $proc -and -not $proc.HasExited) {
        Write-Host "[PROF] Closing cerf (PID=$cerfPid)..."
        Stop-Process -Id $cerfPid -Force -ErrorAction SilentlyContinue
    }
}
catch {
    Write-Host "[PROF] Error during collection: $_" -ForegroundColor Red
    & cmd.exe /c "`"$wprExe`" -cancel >nul 2>&1" | Out-Null
    throw
}

if (-not (Test-Path $etl)) {
    Write-Error "No ETL file produced. See $log"
    exit 1
}

Write-Host "[PROF] Trace captured: $etl"

Write-Host "[PROF] Resolving symbols + extracting hotspots (first run fetches syms, slow)..."
"=== xperf profile -detail ===" | Out-File $log -Append -Encoding utf8
& $xperfExe -i $etl -symbols -tle -tti -a profile -detail 2>&1 `
    | Tee-Object -FilePath $profileTxt `
    | Out-File $log -Append -Encoding utf8

"=== xperf stack -butterfly $MinHits (cerf only) ===" | Out-File $log -Append -Encoding utf8
& $xperfExe -i $etl -symbols -tle -tti -a stack -process cerf.exe -butterfly $MinHits 2>&1 `
    | Tee-Object -FilePath $butterfly `
    | Out-File $log -Append -Encoding utf8

Write-Host ""
Write-Host "=== Output =========================================================="
Write-Host "  ETL (open in WPA):  $etl"
Write-Host "  Top functions:      $profileTxt"
Write-Host "  Butterfly stacks:   $butterfly"
Write-Host "  Full log:           $log"
Write-Host ""
Write-Host "=== Next steps ======================================================"
Write-Host "  GUI drill-down:    wpa.exe `"$etl`""
Write-Host "  Re-extract top N:  xperf -i `"$etl`" -symbols -a profile -detail"
Write-Host "  Per-thread stacks: xperf -i `"$etl`" -symbols -a stack -process cerf.exe"
Write-Host ""
