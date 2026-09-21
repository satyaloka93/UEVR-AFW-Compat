$ErrorActionPreference = 'Stop'
$source = 'F:\ai\GITHUB\CheekyFoveatedDLSS\bin\Release\CheekyFoveatedDLSS.addon64'
$destination = 'F:\SteamLibrary\steamapps\common\TheOuterWorlds2\Arkansas\Binaries\Win64\CheekyFoveatedDLSS.addon64'
if (!(Test-Path $source) -or !(Test-Path $destination)) { throw 'Missing Cheeky build or installed addon' }
# This helper refuses deployment while a Shipping game is running, preserves
# prior backend/logs and verifies the DLL/PDB pair. No configuration writes.
& (Join-Path $PSScriptRoot 'deploy-native-openxr.ps1')
$backup = $destination + '.pre-native-costs-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.bak'
Copy-Item $destination $backup
try {
    Copy-Item $source $destination -Force
    $expected = (Get-FileHash $source -Algorithm SHA256).Hash
    $actual = (Get-FileHash $destination -Algorithm SHA256).Hash
    if ($actual -ne $expected) { throw 'Cheeky addon hash mismatch' }
    Write-Output "CHEEKY_DEPLOYED=$destination SHA256=$actual BACKUP=$backup"
} catch {
    Copy-Item $backup $destination -Force
    throw
}
