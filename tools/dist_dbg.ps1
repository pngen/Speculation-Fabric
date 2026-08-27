$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'build-tests-release\tools'
$port = 47003
$log = Join-Path $env:TEMP 'sf_coord_log.txt'
Get-Process sf_coordinator,sf_worker,sf_driver -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
$coord = Start-Process -FilePath (Join-Path $bin 'sf_coordinator.exe') -ArgumentList $port -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
Start-Sleep -Milliseconds 700
$w1 = Start-Process -FilePath (Join-Path $bin 'sf_worker.exe') -ArgumentList @("127.0.0.1",$port,"proposer") -PassThru
$wv = Start-Process -FilePath (Join-Path $bin 'sf_worker.exe') -ArgumentList @("127.0.0.1",$port,"verifier") -PassThru
Start-Sleep -Milliseconds 700
& (Join-Path $bin 'sf_driver.exe') "127.0.0.1" $port "1" 2>&1 | Out-String | Write-Output
Write-Output "--- phase1 done ---"
Stop-Process -Id $w1.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400
$w2 = Start-Process -FilePath (Join-Path $bin 'sf_worker.exe') -ArgumentList @("127.0.0.1",$port,"proposer") -PassThru
Start-Sleep -Milliseconds 900
& (Join-Path $bin 'sf_driver.exe') "127.0.0.1" $port "2" 2>&1 | Out-String | Write-Output
Start-Sleep -Milliseconds 300
Write-Output "--- coordinator log ---"
if (Test-Path $log) { Get-Content $log }
Get-Process -Id $w2.Id,$wv.Id,$coord.Id -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
