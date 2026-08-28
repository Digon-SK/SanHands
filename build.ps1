param(
    [string]$PluginSdk = 'C:\Users\Digon\Documents\Fuentes\plugin-sdk-master',
    [string]$RwFury = 'C:\Users\Digon\Documents\Fuentes\rwfury-master',
    [string]$GameDir = 'C:\juegos\Grand Theft Auto San Andreas',
    [string]$InstallDir = 'C:\juegos\Grand Theft Auto San Andreas\modloader\hands'
)

$ErrorActionPreference = 'Stop'
$ProjectDir = $PSScriptRoot
$BuildDir = Join-Path $ProjectDir 'build'
$DistDir = Join-Path $ProjectDir 'dist'
$Compiler = 'C:\msys64\mingw32\bin\g++.exe'
$MingwBin = Split-Path -Parent $Compiler

# cc1plus y el enlazador cargan sus DLL auxiliares desde el PATH.
$env:PATH = "$MingwBin;$env:PATH"

New-Item -ItemType Directory -Force -Path $BuildDir, $DistDir, $InstallDir | Out-Null

$IncludeDirs = @(
    (Join-Path $PluginSdk 'plugin_sa'),
    (Join-Path $PluginSdk 'plugin_sa\game_sa'),
    (Join-Path $PluginSdk 'plugin_sa\game_sa\enums'),
    (Join-Path $PluginSdk 'plugin_sa\game_sa\rw'),
    (Join-Path $PluginSdk 'shared'),
    (Join-Path $PluginSdk 'shared\game'),
    (Join-Path $PluginSdk 'injector'),
    (Join-Path $PluginSdk 'hooking'),
    (Join-Path $PluginSdk 'safetyhook')
)

$OutputAsi = Join-Path $DistDir 'SanHands.asi'
$TextureSource = Join-Path $ProjectDir 'assets\GangHands.txd'
if (-not (Test-Path -LiteralPath $TextureSource)) {
    throw "Falta el activo $TextureSource"
}
$Arguments = @(
    '-std=gnu++23', '-m32', '-O2', '-DNDEBUG', '-fpermissive',
    '-Wall', '-Wextra', '-Wpedantic',
    '-DGTASA', '-DPLUGIN_SGV_10US', '-DRW', '-D_CRT_SECURE_NO_WARNINGS',
    '-DTARGET_NAME="SanHands"',
    '-shared', (Join-Path $ProjectDir 'src\SanHands.cpp'),
    '-o', $OutputAsi,
    ('-L' + (Join-Path $PluginSdk 'output\mingw\lib')),
    '-lPlugin', '-static-libgcc', '-static-libstdc++',
    '-Wl,--whole-archive', '-Wl,-Bstatic', '-lwinpthread', '-Wl,-Bdynamic', '-Wl,--no-whole-archive',
    '-Wl,--subsystem,windows', '-Wl,--exclude-all-symbols', '-Wl,--gc-sections', '-s'
)
foreach ($IncludeDir in $IncludeDirs) {
    $Arguments += '-isystem'
    $Arguments += $IncludeDir
}

& $Compiler @Arguments
if ($LASTEXITCODE -ne 0) {
    throw "La compilacion fallo con codigo $LASTEXITCODE"
}

& python (Join-Path $ProjectDir 'tools\prepare_assets.py') `
    --game-dir $GameDir `
    --output-dir $DistDir `
    --rwfury-root $RwFury
if ($LASTEXITCODE -ne 0) {
    throw "La preparacion de activos fallo con codigo $LASTEXITCODE"
}

Copy-Item -Force -LiteralPath $TextureSource -Destination $DistDir

Copy-Item -Force -LiteralPath $OutputAsi, (Join-Path $ProjectDir 'SanHands.ini') -Destination $InstallDir
foreach ($Asset in @('shandl.dff', 'shandr.dff', 'fhandl.dff', 'fhandr.dff', 'ghands.ifp', 'handpose.ifp', 'GangHands.txd', 'hands-assets.json', 'LICENSE.rwfury.txt')) {
    $Source = Join-Path $DistDir $Asset
    if (Test-Path -LiteralPath $Source) {
        Copy-Item -Force -LiteralPath $Source -Destination $InstallDir
    }
}

Write-Host "Compilado e instalado en $InstallDir"
