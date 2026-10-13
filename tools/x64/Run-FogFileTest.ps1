# Compares files read through the native x64 Storm/Fog file API with an independent reader (StormLib via the
# standalone engine's archive code), byte for byte. Needs the user's Diablo II 1.10 install; nothing is copied
# into the repo. Builds the small helper tools it needs into out\fogfile.
param(
    [string]$GameDir = "C:\games\Diablo II 1.10",
    [string]$DllDir = "$PSScriptRoot\..\..\out\build\X64Dlls\runtime\Release"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$work = Join-Path $root "out\fogfile"; New-Item -ItemType Directory -Force $work | Out-Null
$vs = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
$dllDir = (Resolve-Path $DllDir).Path

cmd /c "set PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer&& `"$vs`" x64 >nul 2>&1 && cd /d `"$work`" && cl /nologo /EHsc /std:c++17 /Fe:FogFileTest.exe `"$root\tools\x64\FogFileTest.cpp`" > build_test.txt 2>&1" | Out-Null
if (-not (Test-Path "$work\FogFileTest.exe")) { Get-Content "$work\build_test.txt" | Select-Object -Last 10; throw "build failed" }

$absent = "data\global\excel\does_not_exist.txt" # must be reported missing by both readers
$files = @(
    "data\global\excel\armor.txt", "data\global\excel\armor.bin", "data\global\excel\levels.txt", "data\global\excel\lvlsub.bin",
    "data\global\excel\monstats.txt", "data\global\excel\skills.bin", "data\global\palette\act1\pal.dat",
    "data\global\tiles\act1\town\floor.dt1", "data\local\lng\eng\string.tbl", $absent
)
& "$work\FogFileTest.exe" $dllDir $GameDir "$work\native" @files

# Independent reader: the standalone engine's Archives class, via a tiny tool built on d2assets.
$reader = "$work\mpqx.exe"
if (-not (Test-Path $reader)) {
    @'
#include "Assets.h"
#include <fstream>
int main(int argc, char** argv) {
    d2::Archives archives(argv[1]);
    for (int i = 3; i < argc; ++i) { try { auto b = archives.read(argv[i]); std::ofstream(std::string(argv[2]) + "\\" + [&]{ std::string f = argv[i]; for (char& c : f) if (c=='\\'||c=='/'||c==':') c='_'; return f; }(), std::ios::binary).write((const char*)b.data(), (std::streamsize)b.size()); } catch (...) {} }
}
'@ | Set-Content "$work\mpqx.cpp"
    cmd /c "set PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer&& `"$vs`" x64 >nul 2>&1 && cd /d `"$work`" && cl /nologo /MD /EHsc /std:c++20 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I`"$root\modern\src`" /I`"$root\modern\external\StormLib\src`" mpqx.cpp `"$root\out\modern\Release\d2assets.lib`" `"$root\out\modern\external\StormLib\Release\StormLib.lib`" advapi32.lib user32.lib wininet.lib /Fe:mpqx.exe > build_reader.txt 2>&1" | Out-Null
    if (-not (Test-Path $reader)) { Get-Content "$work\build_reader.txt" | Select-Object -Last 8; throw "reader build failed (run Build-Modern.cmd first)" }
}
New-Item -ItemType Directory -Force "$work\reference" | Out-Null
& $reader $GameDir "$work\reference" @files

$bad = 0; $checked = 0
foreach ($f in $files) {
    $flat = ($f -replace '[\\/:]', '_')
    $a = "$work\native\$flat"; $b = "$work\reference\$flat"
    $ea = Test-Path $a; $eb = Test-Path $b
    if (-not $ea -and -not $eb) {
        # Only the deliberately absent file may be missing from both; anything else means the test proved nothing.
        if ($f -eq $absent) { Write-Host "  (both report missing, as expected) $f"; continue }
        Write-Host "MISSING in both readers: $f"; ++$bad; continue
    }
    if ($ea -ne $eb) { Write-Host "MISMATCH presence: $f native=$ea reference=$eb"; ++$bad; continue }
    ++$checked
    if ((Get-FileHash $a).Hash -ne (Get-FileHash $b).Hash) { Write-Host "MISMATCH content: $f"; ++$bad }
}
if ($bad) { Write-Host "FAIL: $bad mismatch(es)"; exit 1 }
Write-Host "PASS: $checked files identical through the native x64 Storm/Fog file API and the independent reader"
