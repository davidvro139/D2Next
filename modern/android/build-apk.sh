#!/bin/bash
# Builds D2Next for Android (arm64) as out/android/D2Next.apk, with the Android SDK's own tools (no Gradle):
#   the game (CMake + the NDK) -> libmain.so and libSDL3.so
#   SDL's Java activity (external/SDL/android-project) -> classes.dex (javac, d8)
#   the manifest and resources -> the package (aapt2), then aligned and signed with the debug key (zipalign, apksigner)
# Install it on a phone with USB debugging on:  adb install -r out/android/D2Next.apk
# Needs: the Android SDK (ANDROID_HOME, default %LOCALAPPDATA%\Android\Sdk) with platforms;android-36,
# build-tools;36.1.0, ndk;30.0.16248370 and cmake;4.1.2, and a JDK (JAVA_HOME, default the newest in Program Files\Java).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SDK="${ANDROID_HOME:-${LOCALAPPDATA:-$HOME}/Android/Sdk}"
NDK="$SDK/ndk/30.0.16248370"
TOOLS="$SDK/build-tools/36.1.0"
PLATFORM="$SDK/platforms/android-36/android.jar"
CMAKE_BIN="$SDK/cmake/4.1.2/bin"
if [ -z "${JAVA_HOME:-}" ]; then JAVA_HOME="$(ls -d "/c/Program Files/Java/jdk-"* 2>/dev/null | sort -V | tail -1)"; fi
export JAVA_HOME PATH="$JAVA_HOME/bin:$PATH"
OUT="$ROOT/out/android"
CONFIG="${CONFIG:-RelWithDebInfo}"
PROJECT_SLUG="$(basename "$ROOT" | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9._-]/-/g')"
BUILD="${ANDROID_BUILD_DIR:-$ROOT/out/android-arm64-$PROJECT_SLUG}"

path_key() {
  local path="$1"
  if command -v cygpath > /dev/null && [ -n "$path" ]; then
    cygpath -u "$path" 2>/dev/null || printf '%s\n' "$path"
  else
    printf '%s\n' "$path"
  fi | tr '[:upper:]' '[:lower:]'
}
same_path() { [ -n "${1:-}" ] && [ "$(path_key "$1")" = "$(path_key "$2")" ]; }
reset_stale_cmake_cache() {
  local build="$1"
  local source="$2"
  local cached=""
  [ -f "$build/CMakeCache.txt" ] || return 0
  cached="$(grep -m1 '^CMAKE_HOME_DIRECTORY' "$build/CMakeCache.txt" | cut -d= -f2- || true)"
  if ! same_path "$cached" "$source"; then
    echo "== $build was set up for ${cached:-another source}: setting it up again for $source"
    rm -rf "$build/CMakeCache.txt" "$build/CMakeFiles"
  fi
}
reset_stale_cmake_cache "$BUILD" "$ROOT/modern"
mkdir -p "$OUT"
for need in "$NDK" "$TOOLS" "$PLATFORM" "$CMAKE_BIN/cmake.exe" "$JAVA_HOME/bin/javac.exe"; do
  [ -e "$need" ] || [ -e "${need%.exe}" ] || { echo "Missing: $need"; exit 1; }
done
exe() { if [ -e "$1.exe" ]; then echo "$1.exe"; elif [ -e "$1.bat" ]; then echo "$1.bat"; else echo "$1"; fi; }

echo "== native code ($CONFIG)"
"$(exe "$CMAKE_BIN/cmake")" -S "$ROOT/modern" -B "$BUILD" -G Ninja -DCMAKE_MAKE_PROGRAM="$(exe "$CMAKE_BIN/ninja")" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE="$CONFIG" > "$OUT.configure.log" 2>&1 || { tail -20 "$OUT.configure.log"; exit 1; }
"$(exe "$CMAKE_BIN/ninja")" -C "$BUILD" D2Next

echo "== Java"
rm -rf "${OUT:?}/classes" "${OUT:?}/dex" "${OUT:?}/apk" "${OUT:?}/res.zip"
mkdir -p "$OUT/classes" "$OUT/dex" "$OUT/apk/lib/arm64-v8a"
# One quoted path per line (javac reads the file itself, so the paths are in the system's form).
native() { if command -v cygpath > /dev/null; then cygpath -w "$1"; else echo "$1"; fi; }
find "$ROOT/modern/external/SDL/android-project/app/src/main/java" -name '*.java' | while read -r f; do
  printf '"%s"\n' "$(native "$f" | sed 's|\\|/|g')"; done > "$OUT/java-sources.txt"
javac --release 11 -nowarn -encoding UTF-8 -classpath "$PLATFORM" -d "$OUT/classes" @"$OUT/java-sources.txt"
# Relative paths: d8.bat splits its arguments at spaces.
(cd "$OUT/classes" && "$(exe "$TOOLS/d8")" --release --min-api 26 --lib "$PLATFORM" --output ../dex $(find . -name '*.class'))

echo "== package"
"$(exe "$TOOLS/aapt2")" compile --dir "$HERE/res" -o "$OUT/res.zip"
"$(exe "$TOOLS/aapt2")" link -o "$OUT/unaligned.apk" -I "$PLATFORM" --manifest "$HERE/AndroidManifest.xml" \
  --min-sdk-version 26 --target-sdk-version 36 "$OUT/res.zip"
cp "$OUT/dex/classes.dex" "$OUT/apk/"
cp "$BUILD/libmain.so" "$BUILD/external/SDL/libSDL3.so" "$OUT/apk/lib/arm64-v8a/"
# Strip the debug information from the packaged copies (the build folder keeps it for crash stacks).
STRIP="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip"
[ -e "$STRIP.exe" ] && "$STRIP.exe" --strip-unneeded "$OUT/apk/lib/arm64-v8a/"*.so
(cd "$OUT/apk" && jar ufM0 "$OUT/unaligned.apk" classes.dex lib)
"$(exe "$TOOLS/zipalign")" -f -p 4 "$OUT/unaligned.apk" "$OUT/aligned.apk"

echo "== sign"
KEY="${ANDROID_DEBUG_KEYSTORE:-$HOME/.android/debug.keystore}"
if [ ! -e "$KEY" ]; then
  mkdir -p "$(dirname "$KEY")"
  keytool -genkeypair -keystore "$KEY" -storepass android -keypass android -alias androiddebugkey \
    -dname "CN=Android Debug,O=Android,C=US" -keyalg RSA -keysize 2048 -validity 10000
fi
"$(exe "$TOOLS/apksigner")" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android --out "$OUT/D2Next.apk" "$OUT/aligned.apk"
rm -f "$OUT/unaligned.apk" "$OUT/aligned.apk" "$OUT/aligned.apk.idsig"
ls -la "$OUT/D2Next.apk"
echo "Install: \"$SDK/platform-tools/adb\" install -r \"$OUT/D2Next.apk\""
