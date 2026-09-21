# Building this project from WSL

You do **not** compile in WSL. This is a Windows/MSVC project — WSL's gcc/clang cannot build it.
Instead, drive the Windows toolchain from WSL through `powershell.exe` (WSL interop).

## Minimal example

```bash
powershell.exe -NoProfile -Command "
  Set-Location 'F:\ai\GITHUB\UEVR-AFW-JOEY\UEVR-AFW-PUBLISH';
  cmake --build .\build --config Release --target uevr
"
```

First time only (generates `build/`):

```bash
powershell.exe -NoProfile -Command "
  Set-Location 'F:\ai\GITHUB\UEVR-AFW-JOEY\UEVR-AFW-PUBLISH';
  cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DCMAKE_BUILD_TYPE=Release
"
```

## Gotcha zero: VS updates silently kill old build dirs

Visual Studio auto-updates **delete** the previous MSVC toolset and can drop old
Windows SDKs (this happened 2026-07-28⁠–⁠30: MSVC 14.37 and SDK 10.0.22000 vanished;
now 14.44 / 10.0.26100). A `build/` dir configured before the update then fails with
`error MSB8036: The Windows SDK version ... was not found` or
`The CMAKE_CXX_COMPILER ... is not a full path to an existing compiler tool`.
The fix is NOT to install anything — wipe and reconfigure:

```bash
rm -rf /mnt/f/ai/GITHUB/UEVR-AFW-JOEY/UEVR-AFW-PUBLISH/build
# then run the first-time configure below (CMake auto-selects the installed SDK)
```

## The four gotchas

1. **Paths flip sides.** Inside the `powershell.exe` string use Windows paths (`F:\ai\...`).
   Outside it, in bash, use WSL paths (`/mnt/f/ai/...`). Same file, two spellings.
2. **Escape `$` as `\$`** inside the double-quoted command, or bash eats the PowerShell variable
   before PowerShell sees it. Safest: avoid variables entirely and inline literal paths.
3. **Shell state does not persist** between separate bash invocations — a variable set in one
   call is gone in the next. Don't rely on `X=...` from an earlier command.
4. **Builds are slow.** Run in the background and tee to a log, then poll the log:

```bash
powershell.exe -NoProfile -Command "
  Set-Location 'F:\ai\GITHUB\UEVR-AFW-JOEY\UEVR-AFW-PUBLISH';
  cmake --build .\build --config Release --target uevr *>&1 | Tee-Object -FilePath build.log;
  'EXIT ' + \$LASTEXITCODE | Out-File build.log -Append
"
```

Check results from bash (note `*>&1` captures all PowerShell streams; the log is UTF-16, so
strip NULs when grepping):

```bash
grep -iE 'error C[0-9]|error LNK|EXIT' build.log | tr -d '\000' | tail
```

## Build + deploy in one shot

```bash
powershell.exe -NoProfile -Command "
  Set-Location 'F:\ai\GITHUB\UEVR-AFW-JOEY\UEVR-AFW-PUBLISH';
  cmake --build .\build --config Release --target uevr *>&1 | Tee-Object -FilePath build.log;
  if (\$LASTEXITCODE -eq 0) {
    \$src='F:\ai\GITHUB\UEVR-AFW-JOEY\UEVR-AFW-PUBLISH\build\bin\uevr';
    \$dst='C:\Users\gthom\Downloads\UEVR-AFW-JOEY';
    Copy-Item \"\$src\UEVRBackend.dll\" \"\$dst\UEVRBackend.dll\" -Force;
    Copy-Item \"\$src\UEVRBackend.pdb\" \"\$dst\UEVRBackend.pdb\" -Force;
    'DEPLOYED ' + (Get-FileHash \"\$dst\UEVRBackend.dll\" -Algorithm SHA256).Hash | Out-File build.log -Append
  }
"
```

Always copy the **`.pdb` with the `.dll`** — crash symbolization needs the matched pair.

## Editing Windows files from WSL

Reading/editing `/mnt/c/...` and `/mnt/f/...` from bash works fine (`grep`, `sed -i`, editors).
Just verify the result — a `sed -i` that silently matched nothing looks identical to success:

```bash
grep -nE '^VR_RenderingMethod=' "/mnt/c/Users/gthom/AppData/Roaming/UnrealVRMod/<Profile>/config.txt"
```
