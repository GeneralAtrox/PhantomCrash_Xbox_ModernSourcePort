#requires -Version 5.1
[CmdletBinding()]
param([string]$CMake = 'cmake')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-environment.ps1')
$buildDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\port'
try {
    Invoke-WithPortBuildEnvironment {
        & $CMake --build $buildDirectory --config Release --target phantomcrash_port --parallel 4
        if ($LASTEXITCODE -ne 0) {
            throw "Game build failed with exit code $LASTEXITCODE"
        }
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
exit 0
