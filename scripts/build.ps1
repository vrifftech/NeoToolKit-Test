[CmdletBinding()]
param(
    [string]$BuildDir = 'build/native',
    [string]$VcpkgRoot = $env:VCPKG_INSTALLATION_ROOT,
    [string]$Generator = 'Visual Studio 18 2026',
    [int]$Parallel = 2,
    [switch]$StandaloneWrappers
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $PSScriptRoot
if (-not $VcpkgRoot) { throw 'Supply -VcpkgRoot pointing to the bootstrapped vcpkg installation.' }
$Toolchain = Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake'
if (-not (Test-Path -LiteralPath $Toolchain)) { throw "Missing vcpkg toolchain: $Toolchain" }
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $Root $BuildDir }
$Standalone = if ($StandaloneWrappers) { 'ON' } else { 'OFF' }
& cmake -S $Root -B $BuildDir -G $Generator -A x64 -DNEOTOOLKIT_BUILD_GUI=ON `
    "-DNEOTOOLKIT_BUILD_STANDALONES=$Standalone" "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_MANIFEST_MODE=OFF `
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $BuildDir --config Release --parallel $Parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Built NeoToolKit-Test in $BuildDir"
