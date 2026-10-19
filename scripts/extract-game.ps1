#requires -Version 7.4

# Installs the game data into <Root>\artifacts\europe with the C++ extractor
# (build it with scripts\build-port.ps1 first). Extracts the disc only when
# its files are missing or changed and converts the assets only when they
# are missing or from another converter version; -Convert forces a fresh
# conversion and -Verify hashes every extracted file against disc.json.
[CmdletBinding()]
param(
    [string]$Disc = (Join-Path $PSScriptRoot '..\Phantom Crash (Europe).xiso.iso'),
    [string]$Root = (Join-Path $PSScriptRoot '..'),
    [string]$Extractor = (Join-Path $PSScriptRoot '..\build\port\port\Release\phantomcrash_extract.exe'),
    [switch]$Convert,
    [switch]$Verify
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Extractor -PathType Leaf)) {
    throw "Extractor not found: $Extractor. Run scripts\build-port.ps1 first."
}
$arguments = @('install', '--root', (Resolve-Path -LiteralPath $Root).Path, '--quiet')
if (Test-Path -LiteralPath $Disc -PathType Leaf) {
    $arguments += @('--disc', (Resolve-Path -LiteralPath $Disc).Path)
}
if ($Convert) { $arguments += '--convert' }
if ($Verify) { $arguments += '--verify' }
& $Extractor @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Installing the game data failed with exit code $LASTEXITCODE"
}
