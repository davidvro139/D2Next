# Differential test: original 32-bit 1.10f Fog.dll versus the native x64 Fog.dll.
# Needs the user's own Diablo II 1.10 install for the original DLL (it is never copied into the repo).
#
#   .\tools\x64\Run-FogDiff.ps1 [-GameDir "C:\games\Diablo II 1.10"]
param(
    [string]$GameDir = "C:\games\Diablo II 1.10",
    [string]$X64Fog = "$PSScriptRoot\..\..\out\build\X64Dlls\runtime\Release\Fog.dll"
)
$ErrorActionPreference = "Stop"
$vs = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
$work = Join-Path $PSScriptRoot "..\..\out\fogdiff"
New-Item -ItemType Directory -Force $work | Out-Null
$work = (Resolve-Path $work).Path
$src = (Resolve-Path "$PSScriptRoot\FogDiffTest.cpp").Path
$origFog = Join-Path $GameDir "D2SE\CORES\1.10f\Fog.dll"
if (-not (Test-Path $origFog)) { throw "Original 1.10f Fog.dll not found at $origFog" }

foreach ($arch in "x86", "x64") {
    cmd /c "set PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer&& `"$vs`" $arch >nul 2>&1 && cd /d `"$work`" && cl /nologo /EHsc /std:c++17 /Fe:FogDiffTest_$arch.exe /Fo:FogDiffTest_$arch.obj `"$src`" > build_$arch.txt 2>&1" | Out-Null
    if (-not (Test-Path "$work\FogDiffTest_$arch.exe")) { Get-Content "$work\build_$arch.txt" | Select-Object -Last 10; throw "build failed ($arch)" }
}

& "$work\FogDiffTest_x86.exe" $origFog "$work\original_x86.txt" $GameDir
& "$work\FogDiffTest_x64.exe" (Resolve-Path $X64Fog).Path "$work\native_x64.txt" (Split-Path (Resolve-Path $X64Fog).Path)

$a = Get-Content "$work\original_x86.txt"; $b = Get-Content "$work\native_x64.txt"
if ($a.Count -ne $b.Count) { Write-Host "FAIL: line counts differ ($($a.Count) vs $($b.Count))"; exit 1 }
$diffs = 0
for ($i = 0; $i -lt $a.Count; ++$i) {
    if ($a[$i] -ne $b[$i]) {
        if ($diffs -lt 15) { Write-Host "DIFF line $($i+1):`n  original: $($a[$i])`n  native:   $($b[$i])" }
        ++$diffs
    }
}
if ($diffs) { Write-Host "FAIL: $diffs of $($a.Count) cases differ"; exit 1 }
Write-Host "PASS: all $($a.Count) cases identical between the original x86 Fog.dll and the native x64 Fog.dll"
