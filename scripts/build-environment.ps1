# Shared by the full build and the incremental game launcher. Works in Windows
# PowerShell so Store execution aliases are never needed to bootstrap the build.
function Invoke-WithPortBuildEnvironment {
    param([Parameter(Mandatory)][scriptblock]$Action)

    $savedPath = $env:PATH
    $savedSystemBinaries = $env:VCPKG_FORCE_SYSTEM_BINARIES
    try {
        $aliasRoot = [IO.Path]::GetFullPath(
            (Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps')).TrimEnd('\')
        $directories = @($savedPath -split ';' | Where-Object {
            $entry = $_.Trim().Trim('"').TrimEnd('\')
            $entry -and -not $entry.Equals($aliasRoot, [StringComparison]::OrdinalIgnoreCase) -and
                -not $entry.StartsWith($aliasRoot + '\', [StringComparison]::OrdinalIgnoreCase)
        })
        # Remove the alias directory before discovery: vcpkg may inspect every
        # PATH candidate even if a working PowerShell occurs earlier in PATH.
        $env:PATH = $directories -join ';'
        $candidates = @(
            (Join-Path $PSHOME 'pwsh.exe'),
            (Join-Path $env:ProgramFiles 'PowerShell\7\pwsh.exe')
        )
        $candidates += @(Get-Command pwsh.exe -CommandType Application -All -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty Source)
        if (Get-Command Get-AppxPackage -ErrorAction SilentlyContinue) {
            $candidates += @(Get-AppxPackage -Name Microsoft.PowerShell -ErrorAction SilentlyContinue |
                Sort-Object Version -Descending | ForEach-Object {
                    Join-Path $_.InstallLocation 'pwsh.exe'
                })
        }
        $powershell = $candidates | Where-Object {
            Test-Path -LiteralPath $_ -PathType Leaf
        } | Select-Object -First 1
        if (-not $powershell) {
            throw 'A real PowerShell 7 executable is required; the WindowsApps execution alias cannot be used by vcpkg.'
        }
        $env:PATH = (Split-Path -Parent $powershell) + ';' + $env:PATH
        $env:VCPKG_FORCE_SYSTEM_BINARIES = '1'
        Write-Host "Build PowerShell: $powershell"
        & $Action
    } finally {
        $env:PATH = $savedPath
        $env:VCPKG_FORCE_SYSTEM_BINARIES = $savedSystemBinaries
    }
}
