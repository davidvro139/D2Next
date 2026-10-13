# D2Next on Linux

The same game as the Windows SDL build: D2MOO's game libraries linked in, SDL3 for the window, input and sound, and the OpenGL ES 3 renderer (`src/GlBackend.cpp`). 64-bit x86_64 or aarch64.

## Build

```bash
./Build-Linux.sh
```

The result is `out/linux/D2Next`. The script asks CMake for Ninja, SDL's X11 video path, ALSA audio, and OpenGL ES. Wayland, KMS/DRM, PipeWire, JACK, IBus, PulseAudio and XTEST stay off so a stock Debian/Ubuntu box is enough.

Debian/Ubuntu packages:

```text
cmake ninja-build clang libc++-dev libc++abi-dev libc++1 libc++abi1 pkg-config
libasound2-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev
libxfixes-dev libxi-dev libxss-dev libxkbcommon-dev
libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev
```

From Windows with Docker:

```bash
docker run --rm -v "$PWD:/src" -w /src ubuntu:24.04 bash -lc 'D2MODERN_INSTALL_DEPS=1 ./Build-Linux.sh'
```

Equivalent CMake:

```bash
cmake -S modern -B out/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/linux --parallel
ctest --test-dir out/linux --output-on-failure
```

Non-Windows configures already force `D2MODERN_INPROCESS`, `D2MODERN_PLATFORM=SDL` and `D2MODERN_RENDERER=GL`.

## Running

Put legally owned Diablo II 1.10f archives in `out/linux/MPQ/` (the post-build step copies `MPQ/README.txt` there). Character saves go in `out/linux/Save/`. Then:

```bash
./Launch-Modern-Linux.sh
# or
out/linux/D2Next --native
```

`--game-dir PATH` selects another folder of archives. `--profile PATH` selects another save folder.

TCP/IP LAN works the same as on Windows (`--lan-host`, `--lan-join ADDRESS`). See [LAN.md](LAN.md).

## What this build does not do yet

- A packaged `.deb` / Flatpak.
- Wayland as the default video path (pass `-DSDL_WAYLAND=ON` if you have the extra headers).
- The Windows-only native-host tests (`NativeGameHost.exe`). The decoder and simulation tests do run.
