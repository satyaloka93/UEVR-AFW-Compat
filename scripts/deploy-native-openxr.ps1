$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$sourceDir = Join-Path $repo 'build\bin\uevr'
$destinationDir = 'C:\Users\gthom\Downloads\UEVR-AFW-JOEY'
$runningGames = @(Get-Process | Where-Object { $_.ProcessName -like '*Shipping*' })
if ($runningGames.Count -gt 0) {
    throw ('Close games before deployment: ' + (($runningGames | Select-Object -ExpandProperty ProcessName) -join ', '))
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$recordDir = Join-Path $repo ('diagnostics\native-openxr-' + $stamp)
New-Item -ItemType Directory -Path $recordDir | Out-Null
# Preserve evidence before a new game launch truncates the profile log.
$baselineLogs = @{
    'shf-before.log' = 'C:\Users\gthom\AppData\Roaming\UnrealVRMod\SHf-Win64-Shipping\log.txt'
    'shf-config-before.txt' = 'C:\Users\gthom\AppData\Roaming\UnrealVRMod\SHf-Win64-Shipping\config.txt'
    'shf-cvars-before.txt' = 'C:\Users\gthom\AppData\Roaming\UnrealVRMod\SHf-Win64-Shipping\cvars_standard.txt'
    'shf-user-script-before.txt' = 'C:\Users\gthom\AppData\Roaming\UnrealVRMod\SHf-Win64-Shipping\user_script.txt'
    'tow2-before.log' = 'C:\Users\gthom\AppData\Roaming\UnrealVRMod\TheOuterWorlds2-Win64-Shipping\log.txt'
    'vrcompositor-before.txt' = 'C:\Program Files (x86)\Steam\logs\vrcompositor.txt'
    'cheeky-before.log' = 'F:\SteamLibrary\steamapps\common\TheOuterWorlds2\Arkansas\Binaries\Win64\CheekyFoveatedDLSS.log'
    'reshade-before.log' = 'F:\SteamLibrary\steamapps\common\TheOuterWorlds2\Arkansas\Binaries\Win64\ReShade.log'
}
foreach ($entry in $baselineLogs.GetEnumerator()) {
    if (Test-Path $entry.Value) { Copy-Item $entry.Value (Join-Path $recordDir $entry.Key) }
}
$files = @('UEVRBackend.dll', 'UEVRBackend.pdb')
# Validate and back up BOTH originals before replacing either.
foreach ($file in $files) {
    $sourcePath = Join-Path $sourceDir $file
    $destinationPath = Join-Path $destinationDir $file
    if (!(Test-Path $sourcePath) -or !(Test-Path $destinationPath)) { throw "Missing build or installed file: $file" }
    Copy-Item $destinationPath ($destinationPath + '.pre-native-openxr-' + $stamp + '.bak')
}
try {
    foreach ($file in $files) {
        $sourcePath = Join-Path $sourceDir $file
        $destinationPath = Join-Path $destinationDir $file
        Copy-Item $sourcePath $destinationPath -Force
        $expected = (Get-FileHash $sourcePath -Algorithm SHA256).Hash
        $actual = (Get-FileHash $destinationPath -Algorithm SHA256).Hash
        if ($expected -ne $actual) { throw "Hash mismatch: $file" }
        Write-Output "$file SHA256=$actual"
    }
} catch {
    foreach ($file in $files) {
        $destinationPath = Join-Path $destinationDir $file
        Copy-Item ($destinationPath + '.pre-native-openxr-' + $stamp + '.bak') $destinationPath -Force
    }
    throw
}
Write-Output "DEPLOYED=$destinationDir BACKUP_SUFFIX=.pre-native-openxr-$stamp.bak BASELINE=$recordDir"
