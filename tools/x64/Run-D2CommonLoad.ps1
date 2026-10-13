# Loads all game data tables through the ORIGINAL 32-bit D2Common and through the x64 D2Common, and compares the
# loaded records. Needs the user's Diablo II 1.10 install. Nothing from it is copied into the repo.
#
#   .\tools\x64\Run-D2CommonLoad.ps1 [-Hex]      # -Hex dumps record bytes instead of hashes
param(
    [string]$GameDir = "C:\games\Diablo II 1.10",
    [string]$X64Dir = "$PSScriptRoot\..\..\out\build\X64Dlls\runtime\Release",
    [switch]$Hex,
    [switch]$Presets   # also load the .ds1 presets and tile libraries (what a running game does)
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$vs = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
$work = Join-Path $root "out\d2commonload"; New-Item -ItemType Directory -Force $work | Out-Null
$src = "$root\tools\x64\D2CommonLoadTest.cpp"
$core = Join-Path $GameDir "D2SE\CORES\1.10f"
$extraArgs = @(); if ($Hex) { $extraArgs += "--hex" }; if ($Presets) { $extraArgs += "--presets" }

# The x86 reference build reads the MPQs through a 32-bit StormLib (the original Storm.dll cannot open files here).
$stormLibSrc = "$root\modern\external\StormLib"
$stormLib32 = "$work\stormlib-x86"
if (-not (Test-Path "$stormLib32\Release\StormLib.lib")) {
    cmake -S $stormLibSrc -B $stormLib32 -A Win32 -DSTORM_USE_BUNDLED_LIBRARIES=ON -DBUILD_SHARED_LIBS=OFF -DSTORM_SKIP_INSTALL=ON -DSTORM_BUILD_TESTS=OFF | Out-Null
    cmake --build $stormLib32 --config Release | Out-Null
    if (-not (Test-Path "$stormLib32\Release\StormLib.lib")) { throw "32-bit StormLib build failed" }
}

foreach ($arch in "x86", "x64") {
    $extra = if ($arch -eq "x86") { "/MD /I`"$stormLibSrc\src`" `"$stormLib32\Release\StormLib.lib`" advapi32.lib user32.lib" } else { "" }
    cmd /c "set PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer&& `"$vs`" $arch >nul 2>&1 && cd /d `"$work`" && cl /nologo /EHsc /std:c++17 /Fe:D2CommonLoadTest_$arch.exe /Fo:D2CommonLoadTest_$arch.obj `"$src`" $extra > build_$arch.txt 2>&1" | Out-Null
    if (-not (Test-Path "$work\D2CommonLoadTest_$arch.exe")) { Get-Content "$work\build_$arch.txt" | Select-Object -Last 10; throw "build failed ($arch)" }
}

Write-Host "--- original 32-bit"
& "$work\D2CommonLoadTest_x86.exe" $core $GameDir "$work\original_x86.txt" @extraArgs
$x86Exit = $LASTEXITCODE
Write-Host "--- native x64"
& "$work\D2CommonLoadTest_x64.exe" (Resolve-Path $X64Dir).Path $GameDir "$work\native_x64.txt" @extraArgs
$x64Exit = $LASTEXITCODE
if ($x86Exit -ne 0 -or $x64Exit -ne 0) { Write-Host "FAIL: exit codes x86=$x86Exit x64=$x64Exit"; exit 1 }

$a = Get-Content "$work\original_x86.txt"; $b = Get-Content "$work\native_x64.txt"
$diff = 0
$n = [Math]::Max($a.Count, $b.Count)
for ($i = 0; $i -lt $n; ++$i) {
    if ($a[$i] -ne $b[$i]) { if ($diff -lt 12) { Write-Host "DIFF line $($i+1):`n  original: $($a[$i])`n  native:   $($b[$i])" }; ++$diff }
}
$counts = $a | Where-Object { $_ -match "count=" }
Write-Host ($counts -join "; ")
if ($diff) { Write-Host "FAIL: $diff of $n lines differ"; exit 1 }
Write-Host "PASS: $n lines identical between the original 32-bit D2Common and the x64 D2Common"
