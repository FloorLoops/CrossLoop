# Future compile gate only. Run under the main task's build lease.
param(
  [string]$CMakeExe = 'cmake',
  [string]$NinjaExe = 'ninja'
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$cmake = (Get-Command $CMakeExe -ErrorAction Stop).Source
$ninja = (Get-Command $NinjaExe -ErrorAction Stop).Source
$buildDir = Join-Path $here 'build-esp32s3'
$toolchain = Join-Path $here 'toolchain-esp32s3.cmake'
& $cmake -S $here -B $buildDir -G Ninja `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DCMAKE_MAKE_PROGRAM=$ninja"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $buildDir --parallel 2
exit $LASTEXITCODE
