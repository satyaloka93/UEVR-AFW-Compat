$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$profileDir = 'C:\Users\gthom\AppData\Roaming\UnrealVRMod\TheOuterWorlds2-Win64-Shipping'
$legacyLoader = Join-Path $profileDir 'scripts\cvar_loader.lua'
$compatLoader = Join-Path $PSScriptRoot 'validated-cvars\cvar_loader.lua'
if (!(Test-Path $compatLoader)) { throw 'Missing CVar compatibility module.' }
if (@(Get-Process | Where-Object { $_.ProcessName -like '*Shipping*' }).Count -gt 0) {
    throw 'Close games before deployment.'
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$archive = Join-Path $repo ('diagnostics\validated-cvars-' + $stamp)
New-Item -ItemType Directory -Path $archive | Out-Null
foreach ($name in @('config.txt', 'user_script.txt', 'cvars_standard.txt', 'cvars_data.txt')) {
    $path = Join-Path $profileDir $name
    if (Test-Path $path) { Copy-Item $path (Join-Path $archive $name) }
}
$disabledLoader = $legacyLoader + '.disabled-validated-cvars-' + $stamp
$hadLoader = Test-Path $legacyLoader
if ($hadLoader) {
    Copy-Item $legacyLoader (Join-Path $archive 'cvar_loader.lua')
    # Preserve the old loader, but prevent it from silently replaying all the
    # broad profile CVars through PlayerController while the backend owns them.
    Copy-Item $legacyLoader $disabledLoader
}
try {
    # The main profile requires this module. Preserve the import contract even
    # though automatic CVar application is now owned by the backend.
    Copy-Item $compatLoader $legacyLoader -Force
    if ((Get-FileHash $compatLoader).Hash -ne (Get-FileHash $legacyLoader).Hash) {
        throw 'CVar compatibility module hash mismatch.'
    }
    & (Join-Path $PSScriptRoot 'deploy-native-openxr.ps1')
} catch {
    if ($hadLoader) { Copy-Item $disabledLoader $legacyLoader -Force }
    elseif (Test-Path $legacyLoader) { Remove-Item -LiteralPath $legacyLoader }
    throw
}
Write-Output "CVAR_PROFILE_ARCHIVE=$archive LEGACY_LOADER_BACKUP=$disabledLoader"
Write-Output 'Main profile/config/user_script/Cheeky binaries unchanged; cvar_loader import preserved by a no-op compatibility module.'
