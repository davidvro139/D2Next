# Differential test: Storm string and memory helpers, original 32-bit 1.10f Storm.dll versus the native x64 Storm.dll.
# Needs the user's own Diablo II 1.10 install for the original DLL (it is never copied into the repo).
#
#   .\tools\x64\Run-StormMiscDiff.ps1 [-GameDir "C:\games\Diablo II 1.10"]
param(
    [string]$GameDir = "C:\games\Diablo II 1.10",
    [string]$X64Storm = "$PSScriptRoot\..\..\out\build\X64Dlls\runtime\Release\Storm.dll"
)
$ErrorActionPreference = "Stop"
$vs = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
$work = Join-Path $PSScriptRoot "..\..\out\stormmisc"
New-Item -ItemType Directory -Force $work | Out-Null
$work = (Resolve-Path $work).Path
$src = (Resolve-Path "$PSScriptRoot\StormMiscTest.cpp").Path
$origStorm = Join-Path $GameDir "D2SE\CORES\1.10f\Storm.dll"
if (-not (Test-Path $origStorm)) { throw "Original 1.10f Storm.dll not found at $origStorm" }

foreach ($arch in "x86", "x64") {
    cmd /c "set PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer&& `"$vs`" $arch >nul 2>&1 && cd /d `"$work`" && cl /nologo /EHsc /std:c++17 /Fe:StormMiscTest_$arch.exe /Fo:StormMiscTest_$arch.obj `"$src`" > build_$arch.txt 2>&1" | Out-Null
    if (-not (Test-Path "$work\StormMiscTest_$arch.exe")) { Get-Content "$work\build_$arch.txt" | Select-Object -Last 10; throw "build failed ($arch)" }
}

& "$work\StormMiscTest_x86.exe" $origStorm "$work\original_x86.txt"
& "$work\StormMiscTest_x64.exe" (Resolve-Path $X64Storm).Path "$work\native_x64.txt"

$a = Get-Content "$work\original_x86.txt"; $b = Get-Content "$work\native_x64.txt"
if ($a.Count -ne $b.Count -or $a.Count -eq 0) { Write-Host "FAIL: line counts differ or empty ($($a.Count) vs $($b.Count))"; exit 1 }
$diffs = 0
for ($i = 0; $i -lt $a.Count; ++$i) {
    if ($a[$i] -ne $b[$i]) {
        if ($diffs -lt 15) { Write-Host "DIFF line $($i+1):`n  original: $($a[$i])`n  native:   $($b[$i])" }
        ++$diffs
    }
}
if ($diffs) { Write-Host "FAIL: $diffs of $($a.Count) cases differ"; exit 1 }
Write-Host "PASS: all $($a.Count) cases identical between the original x86 Storm.dll and the native x64 Storm.dll"
