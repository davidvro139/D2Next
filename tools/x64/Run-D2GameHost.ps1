# Runs a headless D2Game with a scripted client: the original 32-bit D2Game and the native x64 D2Game, each behind the
# same mock D2Net (tools/x64/MockD2Net.cpp), and compares the packets the server sends. Needs the user's Diablo II
# 1.10 install (the original DLLs and a character save are only read, never copied into the repo).
#
#   .\tools\x64\Run-D2GameHost.ps1 [-Frames 50] [-Seed 0x12345678] [-Save "C:\games\Diablo II 1.10\save\dfsd.d2s"] [-OnlyX64]
param(
    [string]$GameDir = "C:\games\Diablo II 1.10",
    [string]$X64Dir = "$PSScriptRoot\..\..\out\build\X64Dlls\runtime\Release",
    # Comma separated lists: every combination is run and compared
    [string]$Saves = "C:\games\Diablo II 1.10\save\dfsd.d2s,C:\games\Diablo II 1.10\D2SE\CORES\1.10f\save\sfwsedf.d2s",
    [string]$Acts = "0,1,2,3,4",
    [int]$Frames = 50,
    [string]$Seed = "0x12345678",
    # Run targets tried after the fixed walk, for act 1 only: from the start of the Rogue Encampment out of its south exit
    # into the Blood Moor (found from the level's collision map: the walls and buildings need this detour).
    [string]$Route = "5812,4458;5812,4474;5814,4490;5814,4506;5814,4516;5814,4530;5814,4550;5814,4580;5814,4610",
    [switch]$OnlyX64,
    # The 32-bit reference D2Game: this source built as x86 (the original binary needs the original host's containers)
    [string]$X86Game = "$PSScriptRoot\..\..\out\build\LocalGame\runtime\Release\D2Game.dll"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$vs = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
$work = Join-Path $root "out\d2gamehost"; New-Item -ItemType Directory -Force $work | Out-Null
$core = Join-Path $GameDir "D2SE\CORES\1.10f"
$src = "$root\source"

$stormLibSrc = "$root\modern\external\StormLib"
$stormLib32 = "$root\out\d2commonload\stormlib-x86"
if (-not (Test-Path "$stormLib32\Release\StormLib.lib")) {
    cmake -S $stormLibSrc -B $stormLib32 -A Win32 -DSTORM_USE_BUNDLED_LIBRARIES=ON -DBUILD_SHARED_LIBS=OFF -DSTORM_SKIP_INSTALL=ON -DSTORM_BUILD_TESTS=OFF | Out-Null
    cmake --build $stormLib32 --config Release | Out-Null
}

# D2Game's headers, for the host-side structures. Nothing of D2Game is linked.
$includes = "D2Game", "D2Common", "D2CommonDefinitions", "Fog", "Storm", "D2Net", "D2Lang", "D2CMP", "D2Hell" | ForEach-Object { "/I`"$src\$_\include`"" }
$includes += "/I`"$root\external\squall`""
$defs = "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", "/DWHOA_SYSTEM_WIN", "/DD2COMMON_DLL_DECL=", "/DD2GAME_DLL_DECL=", "/DFOG_DLL_DECL=", "/DSTORM_DLL_DECL=", "/DD2NET_DLL_DECL=", "/DD2LANG_DLL_DECL=", "/DD2CMP_DLL_DECL=", "/DD2HELL_DLL_DECL="

$archs = if ($OnlyX64) { @("x64") } else { @("x86", "x64") }
foreach ($arch in $archs) {
    $mockDir = "$work\mock_$arch"; New-Item -ItemType Directory -Force $mockDir | Out-Null
    $extra = if ($arch -eq "x86") { "/MD `"$work\RefStormHost.obj`" `"$stormLib32\Release\StormLib.lib`" advapi32.lib user32.lib" } else { "/MD" }
    $refObj = if ($arch -eq "x86") { "cl /nologo /c /EHsc /std:c++17 /MD /I`"$PSScriptRoot`" /I`"$stormLibSrc\src`" `"$PSScriptRoot\RefStormHost.cpp`" /Fo:RefStormHost.obj > build_ref.txt 2>&1 && " } else { "" }
    $cmd = "set PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer&& `"$vs`" $arch >nul 2>&1 && cd /d `"$work`" && " +
        $refObj + "cl /nologo /LD /EHsc /std:c++17 `"$PSScriptRoot\MockD2Net.cpp`" /Fo:mock_$arch\ /Fe:mock_$arch\D2Net.dll > build_mock_$arch.txt 2>&1 && " +
        "cl /nologo /EHsc /std:c++17 /I`"$PSScriptRoot`" $($includes -join ' ') $($defs -join ' ') `"$PSScriptRoot\D2GameHostTest.cpp`" `"$root\external\squall\storm\thread\CCritSect.cpp`" /Fo:host_$arch\ /Fe:D2GameHostTest_$arch.exe $extra winmm.lib > build_host_$arch.txt 2>&1"
    New-Item -ItemType Directory -Force "$work\host_$arch" | Out-Null
    # A stale executable from an earlier build would hide a compile error.
    foreach ($stale in "$work\D2GameHostTest_$arch.exe", "$mockDir\D2Net.dll") { if (Test-Path $stale) { [IO.File]::Delete($stale) } }
    cmd /c $cmd | Out-Null
    foreach ($f in "$mockDir\D2Net.dll", "$work\D2GameHostTest_$arch.exe") {
        if (-not (Test-Path $f)) { foreach ($l in "build_mock_$arch.txt", "build_host_$arch.txt") { if (Test-Path "$work\$l") { Get-Content "$work\$l" | Select-Object -Last 12 } }; throw "build failed ($arch): $f" }
    }
}

$saveList = $Saves -split ","
$totalDiff = 0; $cases = 0
foreach ($saveFile in $saveList) {
    foreach ($act in ($Acts -split ",")) {
        foreach ($seedValue in ($Seed -split ",")) {
            ++$cases
            $routeArg = if ($act -eq "0") { $Route } else { "-" }
            $tag = "$([IO.Path]::GetFileNameWithoutExtension($saveFile))_act${act}_$seedValue"
            if (-not $OnlyX64) {
                & "$work\D2GameHostTest_x86.exe" $core $GameDir "$work\mock_x86" $saveFile "$work\original_$tag.txt" $Frames $seedValue (Resolve-Path $X86Game).Path $act $routeArg | Out-Null
            }
            & "$work\D2GameHostTest_x64.exe" (Resolve-Path $X64Dir).Path $GameDir "$work\mock_x64" $saveFile "$work\native_$tag.txt" $Frames $seedValue "-" $act $routeArg | Out-Null
            if ($OnlyX64) { continue }
            $a = @(Get-Content "$work\original_$tag.txt"); $b = @(Get-Content "$work\native_$tag.txt")
            $n = [Math]::Max($a.Count, $b.Count); $diff = 0
            for ($i = 0; $i -lt $n; ++$i) {
                if ($a[$i] -ne $b[$i]) { if ($diff -lt 6) { Write-Host "DIFF $tag line $($i+1):`n  original: $($a[$i])`n  native:   $($b[$i])" }; ++$diff }
            }
            $crashed = ($a + $b) | Where-Object { $_ -match "^(CRASH|ASSERT)" }
            Write-Host ("{0,-40} {1,5} lines  {2}" -f $tag, $n, $(if ($diff -or $crashed) { "FAIL ($diff differ)" } else { "identical" }))
            $totalDiff += $diff + @($crashed).Count
        }
    }
}
if ($OnlyX64) { exit 0 }
if ($totalDiff) { Write-Host "FAIL: $totalDiff differences over $cases runs"; exit 1 }
Write-Host "PASS: $cases runs identical between the 32-bit D2Game (this source, x86) and the x64 D2Game"