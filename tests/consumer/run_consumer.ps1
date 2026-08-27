# Speculation Fabric external consumer proof (install + find_package).
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $env:TEMP 'sf_consumer_proof'
$prefix = Join-Path $env:TEMP 'sf_consumer_prefix'

if (Test-Path $build) { Remove-Item $build -Recurse -Force }
if (Test-Path $prefix) { Remove-Item $prefix -Recurse -Force }
New-Item -ItemType Directory -Force -Path $build,$prefix | Out-Null

$bat = @"
@echo off
call "$vcvars" >nul 2>&1
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DSF_ENABLE_CUDA=OFF -DSF_BUILD_TESTS=OFF -DSF_BUILD_EXAMPLES=OFF -DSF_BUILD_CLI=OFF -DCMAKE_INSTALL_PREFIX="$prefix" -S "$root" -B "$build\lib"
if %errorlevel% neq 0 (echo INSTALL_CONFIG_FAIL & exit /b %errorlevel%)
cmake --build "$build\lib" >nul
if %errorlevel% neq 0 (echo LIB_BUILD_FAIL & exit /b %errorlevel%)
cmake --install "$build\lib"
if %errorlevel% neq 0 (echo INSTALL_FAIL & exit /b %errorlevel%)
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DSpeculationFabric_DIR="$prefix\lib\cmake\SpeculationFabric" -S "$root\tests\consumer" -B "$build\consumer"
if %errorlevel% neq 0 (echo CONSUMER_CONFIG_FAIL & exit /b %errorlevel%)
cmake --build "$build\consumer"
if %errorlevel% neq 0 (echo CONSUMER_BUILD_FAIL & exit /b %errorlevel%)
"$build\consumer\sf_consumer.exe"
if %errorlevel% neq 0 (echo CONSUMER_RUN_FAIL & exit /b %errorlevel%)
echo CONSUMER_PROOF_OK
"@
[System.IO.File]::WriteAllText("$build\proof.bat", $bat)
& cmd.exe /c "$build\proof.bat"