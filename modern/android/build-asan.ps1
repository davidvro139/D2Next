param([string]$Sdk="$env:LOCALAPPDATA/Android/Sdk",[string]$Jdk='C:/Program Files/Android/Android Studio/jbr',[int]$Parallel=2,[string]$Name='mod-art-budget-asan')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskBuild=Join-Path $taskRoot 'out/android-asan-arm64'
$taskCmake="$Sdk/cmake/4.1.2/bin/cmake.exe"
$taskNinja="$Sdk/cmake/4.1.2/bin/ninja.exe"
# ASan, not HWASan: the legacy Storm intrusive lists currently require untagged heap pointers.
& $taskCmake -S "$taskRoot/modern" -B $taskBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$taskNinja" "-DCMAKE_TOOLCHAIN_FILE=$Sdk/ndk/30.0.16248370/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DANDROID_STL=c++_shared '-DCMAKE_C_FLAGS=-fsanitize=address -fno-omit-frame-pointer' '-DCMAKE_CXX_FLAGS=-fsanitize=address -fno-omit-frame-pointer' -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address
if($LASTEXITCODE) { throw 'ASan configure failed' }
& $taskCmake --build $taskBuild --target D2Next --parallel $Parallel
if($LASTEXITCODE) { throw 'ASan build failed' }
& (Join-Path $PSScriptRoot 'package-native.ps1') -Sdk $Sdk -Jdk $Jdk -Name $Name -AddressSanitizer
