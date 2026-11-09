param([string]$Sdk="$env:LOCALAPPDATA/Android/Sdk", [string]$Jdk='C:/Program Files/Android/Android Studio/jbr', [string]$Name='mod-foundation',[switch]$AddressSanitizer)
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskBase=Join-Path $taskRoot 'out/android'
$taskOut=$taskBase
$taskBuild=Join-Path $taskRoot 'out/android-arm64-d2next'
$taskManifest="$taskRoot/modern/android/AndroidManifest.xml"
if($AddressSanitizer) {
    $taskOut=Join-Path $taskRoot 'out/android-asan'
    $taskBuild=Join-Path $taskRoot 'out/android-asan-arm64'
    New-Item -ItemType Directory -Force "$taskOut/apk/lib/arm64-v8a" | Out-Null
    [xml]$taskXml=Get-Content -LiteralPath $taskManifest
    $taskNamespace='http://schemas.android.com/apk/res/android'
    foreach($taskSetting in @{debuggable='true';extractNativeLibs='true';gwpAsanMode='never'}.GetEnumerator()) {
        $taskAttribute=$taskXml.CreateAttribute('android',$taskSetting.Key,$taskNamespace)
        $taskAttribute.Value=$taskSetting.Value
        $taskXml.manifest.application.Attributes.SetNamedItem($taskAttribute) | Out-Null
    }
    $taskManifest="$taskOut/AndroidManifest.xml"
    $taskXml.Save($taskManifest)
    Copy-Item -LiteralPath "$Sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" -Destination "$taskOut/apk/lib/arm64-v8a/libc++_shared.so"
    Copy-Item -LiteralPath "$Sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/windows-x86_64/lib/clang/21/lib/linux/libclang_rt.asan-aarch64-android.so" -Destination "$taskOut/apk/lib/arm64-v8a/libclang_rt.asan-aarch64-android.so"
    $taskWrapper=@'
#!/system/bin/sh
HERE="$(cd "$(dirname "$0")" && pwd)"
export ASAN_OPTIONS=log_to_syslog=true,allow_user_segv_handler=1,detect_leaks=0
export LD_PRELOAD="$HERE/libclang_rt.asan-aarch64-android.so $HERE/libc++_shared.so"
exec "$@"
'@.Replace("`r",'')
    [System.IO.File]::WriteAllText("$taskOut/apk/lib/arm64-v8a/wrap.sh",$taskWrapper+"`n",[System.Text.UTF8Encoding]::new($false))
}
if ($Name -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid APK output name' }
# Repackages an already-built native target using Java/resources produced by build-apk.sh.
# Run the full build after any Java or resource changes.
function Invoke-Checked([string]$Tool, [string[]]$Arguments) {
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Tool failed ($LASTEXITCODE)" }
}
foreach ($taskInput in @("$taskBase/res.zip", "$taskBase/dex/classes.dex", "$taskBuild/libmain.so", "$taskBuild/external/SDL/libSDL3.so")) {
    if (!(Test-Path -LiteralPath $taskInput)) { throw "Missing build input: $taskInput" }
}
$taskTools="$Sdk/build-tools/36.1.0"
Invoke-Checked "$taskTools/aapt2.exe" @('link','-o',"$taskOut/$Name-unsigned.apk",'-I',"$Sdk/platforms/android-36/android.jar",'--manifest',$taskManifest,'--min-sdk-version','26','--target-sdk-version','36',"$taskBase/res.zip")
Copy-Item -LiteralPath "$taskBuild/libmain.so" -Destination "$taskOut/apk/lib/arm64-v8a/libmain.so"
Copy-Item -LiteralPath "$taskBuild/external/SDL/libSDL3.so" -Destination "$taskOut/apk/lib/arm64-v8a/libSDL3.so"
Copy-Item -LiteralPath "$taskBase/dex/classes.dex" -Destination "$taskOut/apk/classes.dex"
Invoke-Checked "$Sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe" @('--strip-unneeded',"$taskOut/apk/lib/arm64-v8a/libmain.so","$taskOut/apk/lib/arm64-v8a/libSDL3.so")
Invoke-Checked "$Jdk/bin/jar.exe" @('ufM0',"$taskOut/$Name-unsigned.apk",'-C',"$taskOut/apk",'classes.dex','-C',"$taskOut/apk",'lib')
Invoke-Checked "$taskTools/zipalign.exe" @('-f','-p','4',"$taskOut/$Name-unsigned.apk","$taskOut/$Name-aligned.apk")
Invoke-Checked "$Jdk/bin/java.exe" @('-jar',"$taskTools/lib/apksigner.jar",'sign','--ks',"$env:USERPROFILE/.android/debug.keystore",'--ks-pass','pass:android','--ks-key-alias','androiddebugkey','--key-pass','pass:android','--out',"$taskOut/$Name.apk","$taskOut/$Name-aligned.apk")
Invoke-Checked "$Jdk/bin/java.exe" @('-jar',"$taskTools/lib/apksigner.jar",'verify',"$taskOut/$Name.apk")
Get-FileHash "$taskOut/$Name.apk"
