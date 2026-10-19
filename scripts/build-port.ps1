#requires -Version 7.4

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [switch]$Test
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vcpkgRoot = Join-Path $projectRoot '.tools\vcpkg'
$vcpkgToolchain = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
$buildDirectory = Join-Path $projectRoot 'build\port'
$baseline = (Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'vcpkg.json') |
    ConvertFrom-Json).'builtin-baseline'

function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

. (Join-Path $PSScriptRoot 'build-environment.ps1')
Invoke-WithPortBuildEnvironment {
    # Check out vcpkg at exactly the manifest baseline; binary caching then reuses
    # packages already built by other projects on this machine (for example Lodoss).
    if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot '.git'))) {
        New-Item -ItemType Directory -Force -Path $vcpkgRoot | Out-Null
        Invoke-Checked git @('-C', $vcpkgRoot, 'init', '--quiet')
        Invoke-Checked git @('-C', $vcpkgRoot, 'remote', 'add', 'origin',
            'https://github.com/microsoft/vcpkg.git')
    }
    $head = (& git -C $vcpkgRoot rev-parse --verify --quiet HEAD) 2>$null
    if ($head -ne $baseline) {
        Invoke-Checked git @('-C', $vcpkgRoot, 'fetch', '--depth', '1', 'origin', $baseline)
        Invoke-Checked git @('-C', $vcpkgRoot, 'checkout', '--quiet', '--detach', 'FETCH_HEAD')
    }

    if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
        Invoke-Checked (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') @('-disableMetrics')
    }

    # vcpkg only needs Python 3.7; using the installed interpreter avoids an
    # unnecessary embedded-Python download in locked-down environments.
    $env:VCPKG_FORCE_SYSTEM_BINARIES = '1'

    Invoke-Checked cmake @('-S', $projectRoot, '-B', $buildDirectory,
        '-G', 'Visual Studio 17 2022', '-A', 'x64',
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain")
    Invoke-Checked cmake @('--build', $buildDirectory, '--config', $Configuration)

    if ($Test) {
        Invoke-Checked ctest @('--test-dir', $buildDirectory, '-C', $Configuration,
            '--output-on-failure')
    }
}
