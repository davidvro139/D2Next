param([string]$Serial='',[string]$Sdk="$env:LOCALAPPDATA/Android/Sdk",[string]$GameDir='')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskAdb="$Sdk/platform-tools/adb.exe"
$taskBuild="$taskRoot/out/android-arm64-d2next"
$taskRemote='/data/local/tmp/d2next-tests-'+[guid]::NewGuid().ToString('N')
& $taskAdb -s $Serial get-state
if($LASTEXITCODE) { throw 'Device not available' }
& $taskAdb -s $Serial shell mkdir -p $taskRemote
if($LASTEXITCODE) { throw 'Cannot create test directory' }
$taskResults=@()
$taskTests=@('ArchivePlanTests','AssetTests','TileTests','MapTests','NavigationTests','AnimationTests','OcclusionTests','SkillVisualTests','HirelingPacketTests','BinLayoutTests','RendererLifetimeTests','NativeProtocolTests','SimulationTests','BackendTests')
if($GameDir) { $taskTests+=@('UnitArtBudgetTests','UnitSpriteTests','TableTests') }
foreach($taskTest in $taskTests) {
    & $taskAdb -s $Serial push "$taskBuild/$taskTest" "$taskRemote/$taskTest"
    if($LASTEXITCODE) { throw "Cannot push $taskTest" }
    & $taskAdb -s $Serial shell chmod 755 "$taskRemote/$taskTest"
    if($LASTEXITCODE) { throw "Cannot make $taskTest executable" }
    [string[]]$taskArguments=@()
    if($taskTest -eq 'ArchivePlanTests') { $taskArguments=@("$taskRemote/fixture") }
    elseif($taskTest -in @('UnitArtBudgetTests','UnitSpriteTests','TableTests')) { $taskArguments=@($GameDir) }
    $taskOutput=& $taskAdb -s $Serial shell "$taskRemote/$taskTest" @taskArguments 2>&1
    $taskResults+=[pscustomobject]@{serial=$Serial;test=$taskTest;exit=$LASTEXITCODE;output=($taskOutput -join "`n")}
}
$taskResults | ConvertTo-Json | Set-Content "$taskRoot/out/redmi-core-tests.json"
$taskResults | Format-Table -Wrap
if(@($taskResults | Where-Object exit -NE 0).Count) { throw 'Device tests failed' }
# Keep this unique directory for inspection; never remove a computed remote path recursively.
