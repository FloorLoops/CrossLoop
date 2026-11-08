[CmdletBinding()]
param(
    [ValidateRange(1, 4)]
    [int] $Jobs = 2,

    [string] $GitDirectory,

    [ValidateSet('x4pro', 'x3')]
    [string] $Device = 'x4pro'
)

$ErrorActionPreference = 'Stop'
$buildWorkspace = Split-Path -Parent $PSScriptRoot
$buildPython = Join-Path $buildWorkspace 'x4-build-tools\Scripts\python.exe'
$environment = @{ x4pro = 'x4pro-gh_release'; x3 = 'x3-personal' }[$Device]

if (-not (Test-Path -LiteralPath $buildPython -PathType Leaf)) {
    throw 'The local build environment is missing. See FORK.md for setup.'
}

$previousCore = $env:PLATFORMIO_CORE_DIR
$previousTelemetry = $env:PLATFORMIO_SETTING_ENABLE_TELEMETRY
$previousGitConfig = $env:GIT_CONFIG_PARAMETERS
$previousPath = $env:Path
$previousPythonUtf8 = $env:PYTHONUTF8
try {
    $env:PLATFORMIO_CORE_DIR = Join-Path $buildWorkspace 'x4-pio-core'
    $env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'no'
    # The upstream asset generator reports language names in several scripts.
    $env:PYTHONUTF8 = '1'
    # PlatformIO checks out dependencies in its cache, where paths can exceed 260 characters.
    $env:GIT_CONFIG_PARAMETERS = "$previousGitConfig 'core.longpaths=true'".Trim()
    if ($GitDirectory) {
        $gitPath = Resolve-Path -LiteralPath $GitDirectory
        if (-not (Test-Path -LiteralPath (Join-Path $gitPath 'git.exe') -PathType Leaf)) {
            throw 'GitDirectory must contain git.exe.'
        }
        $env:Path = "$gitPath;$previousPath"
    }
    & $buildPython -m platformio run --project-dir $PSScriptRoot --environment $environment --jobs $Jobs
    if ($LASTEXITCODE -ne 0) {
        throw "$Device build exited with code $LASTEXITCODE."
    }
} finally {
    $env:PLATFORMIO_CORE_DIR = $previousCore
    $env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = $previousTelemetry
    $env:GIT_CONFIG_PARAMETERS = $previousGitConfig
    $env:Path = $previousPath
    $env:PYTHONUTF8 = $previousPythonUtf8
}
