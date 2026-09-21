$ErrorActionPreference = 'Stop'
if (Get-Process -Name 'TheOuterWorlds2*' -ErrorAction SilentlyContinue) { throw 'Close TOW2 before deployment.' }
$root = 'F:\SteamLibrary\steamapps\common\TheOuterWorlds2'
$dir = Join-Path $root 'Arkansas\Binaries\Win64'
$source = Join-Path $root 'renodx-dlss.addon64'
$target = Join-Path $dir 'renodx-dlss.addon64'
$expected = 'FBA3271626587F8B1F49C4FB40BBE23FD969282DA6FE48BE2DA9B17BEB708EE6'
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $expected) { throw 'Source addon changed; inspect before deploying.' }
if (Test-Path -LiteralPath $target) { throw 'Destination already exists; preserve and inspect first.' }
foreach ($name in @('d3d12.dll', 'CheekyFoveatedDLSS.addon64')) {
    $path = Join-Path $dir $name
    if (!(Test-Path -LiteralPath $path)) { throw "Expected test baseline file missing: $path" }
    if (Test-Path -LiteralPath "$path.uevr-nr-test-off") { throw "Backup already exists: $path.uevr-nr-test-off" }
}
# No overwrite and no deletion: retain original binaries under non-loadable suffixes.
[System.IO.File]::Copy($source, $target, $false)
if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expected) { throw 'Deployed hash mismatch.' }
foreach ($name in @('d3d12.dll', 'CheekyFoveatedDLSS.addon64')) {
    $path = Join-Path $dir $name
    Rename-Item -LiteralPath $path -NewName "$name.uevr-nr-test-off"
    Write-Output "Disabled reversibly: $path"
}
Get-FileHash -LiteralPath $target -Algorithm SHA256 | Format-List
Write-Output 'Prepared only: no runtime compatibility or performance test has run. Registry and profile unchanged.'
