param([string]$Serial='',[string]$Sdk="$env:LOCALAPPDATA/Android/Sdk",[string]$Jdk='C:/Program Files/Android/Android Studio/jbr')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskId=[guid]::NewGuid().ToString('N')
$taskOut="$taskRoot/out/save-import-test-$taskId"
New-Item -ItemType Directory -Path "$taskOut/classes","$taskOut/dex" | Out-Null
$taskJava="$taskRoot/modern/external/SDL/android-project/app/src/main/java/org/d2next/game/SaveFileImport.java"
$taskTest="$PSScriptRoot/tests/SaveFileImportTests.java"
$taskPlatform="$Sdk/platforms/android-36/android.jar"
& "$Jdk/bin/javac.exe" --release 11 -encoding UTF-8 -classpath $taskPlatform -d "$taskOut/classes" $taskJava $taskTest
if($LASTEXITCODE) { throw 'Save import test compilation failed' }
Push-Location "$taskOut/classes"
try {
    $taskClassRoot=(Get-Location).Path
    $taskClasses=@(Get-ChildItem -Recurse -Filter '*.class' | ForEach-Object {$_.FullName.Substring($taskClassRoot.Length+1)})
    $env:JAVA_HOME=$Jdk
    & "$Sdk/build-tools/36.1.0/d8.bat" --release --min-api 26 --lib $taskPlatform --output ../dex @taskClasses
    if($LASTEXITCODE) { throw 'Save import test dex compilation failed' }
} finally { Pop-Location }
& "$Jdk/bin/jar.exe" cf "$taskOut/save-import-tests.jar" -C "$taskOut/dex" classes.dex
if($LASTEXITCODE) { throw 'Cannot package save import test' }
$taskAdb="$Sdk/platform-tools/adb.exe"
$taskRemote="/data/local/tmp/d2next-save-import-$taskId.jar"
$taskFixture="/data/local/tmp/d2next-save-import-fixture-$taskId"
& $taskAdb -s $Serial push "$taskOut/save-import-tests.jar" $taskRemote
if($LASTEXITCODE) { throw 'Cannot push save import test' }
& $taskAdb -s $Serial shell dalvikvm -cp $taskRemote org.d2next.game.SaveFileImportTests $taskFixture > "$taskOut/test.log" 2>&1
$taskResult=$LASTEXITCODE
Copy-Item -LiteralPath "$taskOut/test.log" -Destination "$taskRoot/out/redmi-save-import-tests.log"
Get-Content -LiteralPath "$taskOut/test.log"
if($taskResult) { throw 'On-device save import regression failed' }
@{serial=$Serial;fixture=$taskFixture;jarSha256=(Get-FileHash "$taskOut/save-import-tests.jar").Hash;exit=$taskResult} | ConvertTo-Json | Set-Content "$taskOut/result.json"
# Retain this unique fixture for inspection. No original game files or saves used.
