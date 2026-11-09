# One-command check: desktop builds, the 14 desktop tests, ModChainTests, the table dump comparison (.bin vs .txt) and,
# unless -NoAndroid, the Android APK build. Needs the user's own Diablo II 1.10 install (-GameDir).
#
#   .\tools\check.ps1 [-GameDir "D:\gry\Diablo II"] [-NoAndroid]
param(
    [string]$GameDir = "D:\gry\Diablo II",
    [switch]$NoAndroid
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path "$PSScriptRoot\..").Path
$out = Join-Path $repo "out"
$vc = (Get-ChildItem "C:\Program Files*\Microsoft Visual Studio" -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
if (-not $vc) { throw "vcvars64.bat not found (Visual Studio C++ tools)" }
$failed = @()
function Step([string]$name, [scriptblock]$body) {
    Write-Host "== $name"
    $start = Get-Date
    try { & $body; if ($LASTEXITCODE) { throw "exit $LASTEXITCODE" }; Write-Host "   ok ($([int]((Get-Date) - $start).TotalSeconds) s)" }
    catch { Write-Host "   FAILED: $_"; $script:failed += $name }
}
function InVs([string]$command) { cmd /c "`"$vc`" >nul 2>nul && cd /d `"$repo`" && $command" }

Step "desktop build (assets64)" { InVs "cmake --build out/assets64" | Select-String -Pattern "error" }
Step "desktop build (in-process)" { InVs "cmake --build out/desktop-inproc" | Select-String -Pattern "error" }
Step "desktop tests" { InVs "cd out/assets64 && ctest --output-on-failure" | Select-String -Pattern "tests passed|Failed|FAIL" }
Step "x64 DLLs" { InVs "cmake --build out/x64dlls && cd out/x64dlls && ctest" | Select-String -Pattern "tests passed|Failed" }
Step "ModChainTests" { & "$out\assets64\ModChainTests.exe" $GameDir $out }
Step "tables .bin vs .txt" {
    Push-Location $env:TEMP
    try {
        & "$out\desktop-inproc\TableDump.exe" $GameDir bin "$out\tables-bin" | Out-Null
        & "$out\desktop-inproc\TableDump.exe" $GameDir txt "$out\tables-txt" | Out-Null
    } finally { Pop-Location }
    python "$repo\tools\compare-tables.py" "$out\tables-bin" "$out\tables-txt" | Select-Object -Last 5
}
if (-not $NoAndroid) {
    Step "Android APK" {
        $env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
        bash "$repo/modern/android/build-apk.sh" | Select-Object -Last 2
    }
}
if ($failed.Count) { Write-Host "FAILED: $($failed -join ', ')"; exit 1 }
Write-Host "ALL OK"
