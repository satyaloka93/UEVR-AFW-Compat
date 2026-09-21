param([switch]$CaptureDump)
$ErrorActionPreference = 'Stop'
$profileDir = 'C:\Users\gthom\AppData\Roaming\UnrealVRMod\TheOuterWorlds2-Win64-Shipping'
$evidenceDir = Join-Path $profileDir ('nr-hang-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $evidenceDir | Out-Null
foreach ($name in @('log.txt', 'config.txt', 'reshade-addon-config.ini')) {
    $path = Join-Path $profileDir $name
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $evidenceDir }
}
Write-Output "Evidence preserved: $evidenceDir"
$games = @(Get-Process -Name 'TheOuterWorlds2-Win64-Shipping' -ErrorAction SilentlyContinue)
if ($games.Count -eq 0) {
    Write-Output 'TOW2 is closed. No live modules or thread stacks captured; no old dump reused.'
    exit 0
}
if ($games.Count -ne 1) { throw 'Multiple TOW2 processes; identify the frozen process first.' }
$game = $games[0]
$game | Select-Object ProcessName,Id,Path,StartTime,Responding |
    Format-List | Out-File (Join-Path $evidenceDir 'process.txt')
try {
    $modules = @($game.Modules)
    $modules | Select-Object ModuleName,FileName,BaseAddress,ModuleMemorySize |
        Export-Csv -NoTypeInformation -Path (Join-Path $evidenceDir 'modules.csv')
    $modules | Where-Object { $_.ModuleName -match 'UEVR|Reno|Cheeky|XRFrameBridge|OFXR' } |
        ForEach-Object { Get-FileHash -LiteralPath $_.FileName -Algorithm SHA256 } |
        Export-Csv -NoTypeInformation -Path (Join-Path $evidenceDir 'module-hashes.csv')
} catch { Write-Warning "Module inspection failed: $_" }
if ($CaptureDump) {
    $dumper = 'C:\Users\gthom\Downloads\SysinternalsSuite\procdump.exe'
    if (!(Test-Path -LiteralPath $dumper)) { throw 'ProcDump not found.' }
    & $dumper -accepteula -mm $game.Id (Join-Path $evidenceDir 'tow2-nr-hang.dmp')
    if ($LASTEXITCODE -ne 0) { throw "ProcDump failed ($LASTEXITCODE); no successful dump claimed." }
} else { Write-Output 'No dump requested. Use -CaptureDump while frozen for thread stacks.' }
