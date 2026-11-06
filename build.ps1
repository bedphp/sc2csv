#Requires -Version 5.1
<#
.SYNOPSIS
    Configure and build sc2csv, in Debug or Release.

.DESCRIPTION
    Finds Visual Studio (Build Tools or a full install), imports its x64
    developer environment, then configures and builds with the CMake/Ninja
    that ship with Visual Studio (or with cmake/ninja on PATH).

    All paths are derived from this script's location. Each configuration
    gets its own build tree (build for Release, build-debug for Debug),
    because Ninja takes the configuration when it configures, not when it
    builds. The second tree reuses the sources the first one fetched, so
    only the first build needs the network.

.PARAMETER Config
    Debug or Release. Defaults to Release.

.PARAMETER BuildDir
    Build directory. Defaults to <repo>\build (Release) or <repo>\build-debug (Debug).

.PARAMETER Configure
    Force a CMake configure step before building.

.PARAMETER ConfigureOnly
    Configure without building (used by configure.bat).

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Config Debug
    .\build.ps1 -Configure
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',
    [string]$BuildDir = "",
    [switch]$Configure,
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'

function Write-Step([string]$Text) {
    Write-Host "==> $Text" -ForegroundColor Cyan
}

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $Root $(if ($Config -eq 'Debug') { 'build-debug' } else { 'build' })
} elseif (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $Root $BuildDir
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $path = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and $path) {
            return ($path | Select-Object -First 1).Trim()
        }
    }

    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\Community'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Community'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2019\BuildTools'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2019\Community')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'Common7\Tools\VsDevCmd.bat')) {
            return $candidate
        }
    }
    return $null
}

function Import-VsEnvironment([string]$VsPath) {
    $devCmd = Join-Path $VsPath 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $devCmd)) {
        throw "VsDevCmd.bat not found under: $VsPath"
    }

    $lines = & cmd.exe /s /c "`"$devCmd`" -arch=x64 -no_logo >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd.bat failed: $devCmd"
    }
    foreach ($line in $lines) {
        $eq = $line.IndexOf('=')
        if ($eq -le 0) { continue }
        $name = $line.Substring(0, $eq)
        $value = $line.Substring($eq + 1)
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}

function Find-Tool([string]$VsPath, [string]$RelativePath, [string]$Name) {
    if ($VsPath) {
        $candidate = Join-Path $VsPath $RelativePath
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return $null
}

Write-Step "Project: $Root"

$vsPath = Find-VisualStudio
if ($vsPath) {
    Write-Step "Visual Studio: $vsPath"
    Import-VsEnvironment $vsPath
} else {
    Write-Warning "Visual Studio was not found; trying cmake and ninja from PATH."
}

$cmake = Find-Tool $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' 'cmake'
if (-not $cmake) {
    throw "cmake.exe was not found (neither in Visual Studio nor on PATH)."
}
$ninja = Find-Tool $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' 'ninja'

Write-Step "CMake: $cmake"
if ($ninja) { Write-Step "Ninja: $ninja" }
Write-Step "Configuration: $Config"
Write-Step "Build directory: $BuildDir"

$cache = Join-Path $BuildDir 'CMakeCache.txt'
$needConfigure = $Configure.IsPresent -or $ConfigureOnly.IsPresent -or
                 (-not (Test-Path -LiteralPath $cache))

if ($needConfigure) {
    $configureArgs = @(
        '-S', $Root,
        '-B', $BuildDir,
        '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=$Config"
    )
    if ($ninja) {
        $configureArgs += "-DCMAKE_MAKE_PROGRAM=$ninja"
    }

    # FetchContent only downloads for a tree that has no sources yet. Point a second tree (Debug next to Release) at the
    # sources the first one already fetched, so only the first build of a checkout needs the network.
    $sharedDeps = Join-Path $Root 'build\_deps'
    if ((Test-Path -LiteralPath $sharedDeps) -and
        ($BuildDir -ne [System.IO.Path]::GetFullPath((Join-Path $Root 'build')))) {
        foreach ($dependency in @(@{ Name = 'glfw'; Source = 'glfw-src' }, @{ Name = 'imgui'; Source = 'imgui-src' })) {
            $source = Join-Path $sharedDeps $dependency.Source
            if (Test-Path -LiteralPath $source) {
                $configureArgs += "-DFETCHCONTENT_SOURCE_DIR_$($dependency.Name.ToUpperInvariant())=$source"
                Write-Step "Reusing $($dependency.Source) from $sharedDeps"
            }
        }
    }

    Write-Step "Configuring"
    & $cmake @configureArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }
}

if ($ConfigureOnly) {
    Write-Step "Configure-only run finished."
    exit 0
}

Write-Step "Building"

# link.exe overwrites the executable in place, and NTFS keeps the creation time of a file that is opened and truncated,
# so a rebuilt exe looks old in Explorer. Removing it first makes the link write a brand new file whose creation and
# modification times are both this build's.
$exe = Join-Path $BuildDir 'bin\sc2csv.exe'
if (Test-Path -LiteralPath $exe) {
    try {
        Remove-Item -LiteralPath $exe -Force
        Write-Step "Removed the old $(Split-Path -Leaf $exe); the link will create a new one"
    } catch {
        Write-Warning "Could not remove $exe (running?); the link will overwrite it instead."
    }
}

& $cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path -LiteralPath $exe)) {
    throw "The build succeeded but $exe is missing."
}

Write-Step "Done ($Config): $exe"
