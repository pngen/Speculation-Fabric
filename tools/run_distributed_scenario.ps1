# Atomic distributed stale-authority closure scenario (worker kill + restart).
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'build-tests-release\tools'
$port = 47004
$tmp = $env:TEMP
$cLog = Join-Path $tmp 'sf_coord_log.txt'
$ready = Join-Path $tmp 'sf_restart_ready'
$done  = Join-Path $tmp 'sf_restart_done'
Remove-Item $ready,$done,$cLog -ErrorAction SilentlyContinue

Get-Process sf_coordinator,sf_worker,sf_driver -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

$coord = Start-Process -FilePath (Join-Path $bin 'sf_coordinator.exe') -ArgumentList $port -PassThru -RedirectStandardOutput $cLog -RedirectStandardError "$cLog.err"
Start-Sleep -Milliseconds 700
$wLog = Join-Path $tmp 'sf_worker_log.txt'
$w1 = Start-Process -FilePath (Join-Path $bin 'sf_worker.exe') -ArgumentList @("127.0.0.1",$port,"proposer") -PassThru -RedirectStandardOutput $wLog -RedirectStandardError "$wLog.err"
$wv = Start-Process -FilePath (Join-Path $bin 'sf_worker.exe') -ArgumentList @("127.0.0.1",$port,"verifier") -PassThru -RedirectStandardOutput "$wLog.v" -RedirectStandardError "$wLog.verr"
Start-Sleep -Milliseconds 700

# Driver runs in the background; it submits A-D, then waits for the restart.
$dLog = Join-Path $tmp 'sf_driver_log.txt'
$drv = Start-Process -FilePath (Join-Path $bin 'sf_driver.exe') -ArgumentList @("127.0.0.1",$port,$tmp) -PassThru -RedirectStandardOutput $dLog -RedirectStandardError "$dLog.err"

# Wait for the driver to reach the restart point.
$deadline = (Get-Date).AddSeconds(30)
while (!(Test-Path $ready)) {
  if ((Get-Date) -gt $deadline) { break }
  Start-Sleep -Milliseconds 100
}

# Kill the proposer worker as an actual OS process, then restart it.
if (Test-Path $ready) {
  Stop-Process -Id $w1.Id -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 400
  $w2 = Start-Process -FilePath (Join-Path $bin 'sf_worker.exe') -ArgumentList @("127.0.0.1",$port,"proposer") -PassThru -RedirectStandardOutput "$wLog.w2" -RedirectStandardError "$wLog.w2err"
  Start-Sleep -Milliseconds 900
  Set-Content -Path $done -Value "done"
}

# Wait for the driver to finish.
if (-not $drv.WaitForExit(45000)) { Stop-Process -Id $drv.Id -Force -ErrorAction SilentlyContinue }
$code = $drv.ExitCode
Start-Sleep -Milliseconds 300

Get-Process -Id $w1.Id,$wv.Id,$w2.Id,$coord.Id -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Write-Output "driverExit=$code"
$ok = (Test-Path $dLog) -and ((Get-Content $dLog -Raw) -match "ALL_OK")
if (Test-Path $dLog) { Write-Output "--- driver log ---"; Get-Content $dLog }
if (Test-Path $wLog) { Write-Output "--- worker w1 log ---"; Get-Content $wLog }
if (Test-Path "$wLog.w2") { Write-Output "--- worker w2 log ---"; Get-Content "$wLog.w2" }
if (Test-Path "$wLog.w2err") { Write-Output "--- worker w2 err ---"; Get-Content "$wLog.w2err" }
if (Test-Path $cLog) { Write-Output "--- coordinator log ---"; Get-Content $cLog }
if ($ok) { Write-Output "DISTRIBUTED_SCENARIO_OK"; exit 0 }
Write-Output "DISTRIBUTED_SCENARIO_FAIL"; exit 1