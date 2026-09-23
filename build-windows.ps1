<#
.SYNOPSIS
  Build Cesium Terrain Builder on Windows with MSVC, Ninja and vcpkg.

.DESCRIPTION
  Uses the Visual Studio installation found by vswhere, and the vcpkg bundled
  with it unless VCPKG_ROOT is set. Dependencies (GDAL, zlib) come from
  vcpkg.json and are installed into .\vcpkg_installed on the first run, which
  takes a while. Executables, ctb.dll and all dependency DLLs end up in
  .\build\bin.

.EXAMPLE
  .\build-windows.ps1
  .\build-windows.ps1 -Config Debug
#>
param(
  [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
  [string]$Config = 'Release',
  [string]$BuildDir = "$PSScriptRoot\build"
)

$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the C++ x64 tools was found.' }

# Set up the MSVC x64 environment (cl, link, cmake, ninja on PATH)
if (-not $env:VSCMD_VER) {
  $env:PATH = "$(Split-Path $vswhere);$env:PATH"   # VsDevCmd calls vswhere by name
  Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
  Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

$vcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { "$vs\VC\vcpkg" }

cmake -S $PSScriptRoot -B $BuildDir -G Ninja `
  "-DCMAKE_BUILD_TYPE=$Config" `
  "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
  '-DVCPKG_TARGET_TRIPLET=x64-windows' `
  "-DVCPKG_INSTALLED_DIR=$PSScriptRoot\vcpkg_installed"
if ($LASTEXITCODE) { exit $LASTEXITCODE }

cmake --build $BuildDir
if ($LASTEXITCODE) { exit $LASTEXITCODE }

Write-Host ''
Write-Host "Built into $BuildDir\bin. Before running the tools, set:"
Write-Host "  `$env:PROJ_DATA = '$PSScriptRoot\vcpkg_installed\x64-windows\share\proj'"
Write-Host "  `$env:GDAL_DATA = '$PSScriptRoot\vcpkg_installed\x64-windows\share\gdal'"
