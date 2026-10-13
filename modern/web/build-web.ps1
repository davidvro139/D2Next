# Builds D2Next for the browser (wasm64 + WebGL 2) on Windows with nothing to install beforehand: no Docker, no Python.
#
#   powershell -ExecutionPolicy Bypass -File modern\web\build-web.ps1          (or Build-Web.cmd)
#
# The first run downloads what the build needs into out\web-tools (about 1.5 GB, once): the Emscripten SDK, the private
# copy of Python that the SDK's compiler runs on (the official "embeddable" zip, kept inside the SDK folder; nothing
# is installed on the system), Ninja, and CMake when the installed one is missing or older than 3.25.
# The result goes to out\web-dist: D2Next.html / .js / .wasm and Play.cmd, ready to copy anywhere or put on a website.
param(
    [string]$Config = "Release",
    [switch]$Clean
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue" # Invoke-WebRequest is many times slower with its progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$EmsdkVersion = "4.0.10"
$PythonVersion = "3.13.3"   # the folder emsdk.bat looks in first: python\3.13.3_64bit
$NinjaVersion = "1.12.1"
$CMakeVersion = "3.30.5"

$project = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$tools = Join-Path $project "out\web-tools"
$build = Join-Path $project "out\web-build"
$dist = Join-Path $project "out\web-dist"
New-Item -ItemType Directory -Force $tools | Out-Null

function Step($text) { Write-Host ""; Write-Host "== $text" -ForegroundColor Yellow }
function Fetch($url, $zip, $into) {
    $file = Join-Path $tools $zip
    if (-not (Test-Path $file)) {
        Write-Host "   downloading $url"
        Invoke-WebRequest $url -OutFile "$file.part" -UseBasicParsing
        Move-Item "$file.part" $file -Force
    }
    if ($into) {
        Write-Host "   unpacking $zip"
        Expand-Archive $file -DestinationPath $into -Force
    }
}

# --- The Emscripten SDK, with its own Python ---
$emsdk = Join-Path $tools "emsdk"
if (-not (Test-Path (Join-Path $emsdk "emsdk.bat"))) {
    Step "Emscripten SDK $EmsdkVersion"
    Fetch "https://github.com/emscripten-core/emsdk/archive/refs/tags/$EmsdkVersion.zip" "emsdk-$EmsdkVersion.zip" $tools
    Move-Item (Join-Path $tools "emsdk-$EmsdkVersion") $emsdk
}
# emsdk.bat runs the first python.exe it finds in its own python\ folder (the name differs between SDK versions, e.g.
# python\3.13.3-0_64bit), else whatever "python" is on PATH; the private Python goes into the first folder it tries.
$folder = [regex]::Match((Get-Content (Join-Path $emsdk "emsdk.bat") -Raw), 'python\\([^\\"]+)\\python\.exe').Groups[1].Value
if (-not $folder) { $folder = "$($PythonVersion)_64bit" }
$python = Join-Path $emsdk "python\$folder"
if (-not (Test-Path (Join-Path $python "python.exe"))) {
    Step "Private Python $PythonVersion for the SDK (not installed on the system)"
    New-Item -ItemType Directory -Force $python | Out-Null
    Fetch "https://www.python.org/ftp/python/$PythonVersion/python-$PythonVersion-embed-amd64.zip" "python-$PythonVersion-embed.zip" $python
    # The embeddable Python ignores PYTHONPATH and the current folder unless its ._pth file says otherwise; the SDK's
    # scripts import their neighbours, which "import site" plus the script folder allows.
    Get-ChildItem $python -Filter "python*._pth" | ForEach-Object {
        (Get-Content $_.FullName) -replace "^#\s*import site", "import site" | Set-Content $_.FullName -Encoding ASCII
    }
}
$emsdkBat = Join-Path $emsdk "emsdk.bat"
$marker = Join-Path $emsdk ".d2next-$EmsdkVersion"
if (-not (Test-Path $marker)) {
    Step "Installing Emscripten $EmsdkVersion (compiler, Node.js; about 1 GB)"
    $env:MSYSTEM = $null; $env:SHELL = $null
    & cmd /c "`"$emsdkBat`" install $EmsdkVersion"
    if ($LASTEXITCODE) { throw "emsdk install failed" }
    & cmd /c "`"$emsdkBat`" activate $EmsdkVersion"
    if ($LASTEXITCODE) { throw "emsdk activate failed" }
    if (-not (Test-Path (Join-Path $emsdk "upstream\emscripten\emcc.bat"))) { throw "The Emscripten SDK did not install (no upstream\emscripten\emcc.bat); see the messages above." }
    Set-Content $marker "ok"
}

# --- Ninja and CMake ---
$ninjaDir = Join-Path $tools "ninja-$NinjaVersion"
if (-not (Test-Path (Join-Path $ninjaDir "ninja.exe"))) {
    Step "Ninja $NinjaVersion"
    New-Item -ItemType Directory -Force $ninjaDir | Out-Null
    Fetch "https://github.com/ninja-build/ninja/releases/download/v$NinjaVersion/ninja-win.zip" "ninja-$NinjaVersion.zip" $ninjaDir
}
$cmakeBin = $null
$installed = Get-Command cmake -ErrorAction SilentlyContinue
if ($installed) {
    $version = [version](((& cmake --version)[0] -split " ")[2] -replace "-.*", "")
    if ($version -ge [version]"3.25") { $cmakeBin = Split-Path $installed.Source }
}
if (-not $cmakeBin) {
    $cmakeDir = Join-Path $tools "cmake-$CMakeVersion-windows-x86_64"
    if (-not (Test-Path (Join-Path $cmakeDir "bin\cmake.exe"))) {
        Step "CMake $CMakeVersion"
        Fetch "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/cmake-$CMakeVersion-windows-x86_64.zip" "cmake-$CMakeVersion.zip" $tools
    }
    $cmakeBin = Join-Path $cmakeDir "bin"
}

# --- Configure and build ---
if ($Clean -and (Test-Path $build)) { Step "Removing the old build"; Remove-Item -Recurse -Force $build }
Step "Building D2Next for the browser ($Config)"
$flags = "-sMEMORY64=1 -fwasm-exceptions"
$configure = "call emcmake cmake -S `"$project\modern`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=$Config -DCMAKE_CXX_SCAN_FOR_MODULES=OFF " +
    "-DCMAKE_C_FLAGS=`"$flags`" -DCMAKE_CXX_FLAGS=`"$flags`" -DSDL_CAMERA=OFF -DSDL_JACK=OFF -DSDL_PIPEWIRE=OFF -DSDL_IBUS=OFF " +
    "-DSDL_HIDAPI_LIBUSB=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF"
$script = Join-Path $tools "build-web.cmd"
Set-Content $script -Encoding ASCII -Value @(
    "@echo off",
    "rem A shell such as Git Bash leaves these behind; emsdk then writes Unix-style paths.",
    "set MSYSTEM=",
    "set SHELL=",
    "set PATH=$ninjaDir;$cmakeBin;%PATH%",
    "call `"$emsdk\emsdk_env.bat`" >nul",
    "if not exist `"$build\build.ninja`" ( $configure || exit /b 1 )",
    "cmake --build `"$build`" || exit /b 1"
)
& cmd /c "`"$script`""
if ($LASTEXITCODE) { throw "The build failed (see above)." }

# --- What players get ---
Step "Collecting the result in out\web-dist"
New-Item -ItemType Directory -Force $dist | Out-Null
foreach ($file in "D2Next.html", "D2Next.js", "D2Next.wasm") { Copy-Item (Join-Path $build $file) $dist -Force }
Copy-Item (Join-Path $PSScriptRoot "serve.ps1") $dist -Force
Set-Content (Join-Path $dist "Play.cmd") -Encoding ASCII -Value @(
    "@echo off",
    "rem Starts a small web server for D2Next on this computer and opens it in the browser. Nothing to install.",
    "powershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0serve.ps1`" %*"
)
$size = (Get-ChildItem $dist | Measure-Object Length -Sum).Sum / 1MB
Write-Host ""
Write-Host ("Done: out\web-dist ({0:N1} MB). Run Play.cmd there, or Play-Web.cmd in the project folder." -f $size) -ForegroundColor Green
