# D2Next in the browser

D2Next runs in a desktop browser as WebAssembly (wasm64) with WebGL 2. Players need nothing installed. Building it on
Windows needs nothing installed either: no Docker and no Python.

## Playing

1. Run `Play-Web.cmd`. This is `Play.cmd` in a copied `out/web-dist` folder. It starts a small web server on this
   computer using the PowerShell that comes with Windows, then opens the page. Keep its window open while you play.
2. Click **Choose Diablo II Folder** and pick the folder where Diablo II, with Lord of Destruction and patch 1.10, is
   installed. You can also drop the `.mpq` files on the page. The page copies the archives it needs into the browser's
   storage once:
   - required: `d2data`, `d2exp`, `d2char`, `d2sfx` and `patch_d2`;
   - used when present: speech and music.

   Nothing is uploaded anywhere. The browser may still ask whether to "upload" the folder, because that's its wording
   for letting a page read files.
3. Click **Play**. The game's own menus follow: choose or create a character there.

Characters live in the browser as well. You can drop `.d2s` files on the Characters box to bring them in, and download
any character to keep a copy. **Clearing this site's data in the browser deletes them.**

`out/web-dist` is a plain static site, so any web server works, and so does a website. `node modern/web/serve.js`
does the same job on other systems.

Requirements: current Chrome or Edge, or Firefox 134 or newer, on a desktop computer. The page needs WebAssembly 64-bit
memory, WebGL 2 and IndexedDB, and tells you which of these is missing. Safari does not have 64-bit WebAssembly memory
yet.

## Building

Windows: run `Build-Web.cmd`.

- The first run downloads what it needs into `out/web-tools`, about 1.5 GB, only once:
  - the Emscripten SDK 4.0.10;
  - the official embeddable Python, which the SDK's compiler needs. It is kept inside the SDK's folder and nothing is
    installed on the system;
  - Ninja;
  - CMake, only if the installed one is older than 3.25.
- The build itself goes to `out/web-build`.
- What players need is copied to `out/web-dist`: `D2Next.html`, `.js` and `.wasm`, plus `Play.cmd` and `serve.ps1`.
- Use `Build-Web.cmd -Clean` to rebuild from scratch.

Linux and macOS: with the Emscripten SDK active, plus CMake 3.25 or newer and Ninja, run
`bash modern/web/build-web.sh`.

## How it fits together

- **Archives.** The MPQs are kept as blobs in IndexedDB (database `d2next`, store `mpq`). They stay outside the game's
  file system, so opening the page is instant. **Play** reads them into memory under `/MPQ` with a progress bar. The
  memory file system takes the arrays as they are (`canOwn`), so there is no second copy. The first version of this page
  kept everything in an IDBFS mount at `/persist`; its files are moved over once and the old database is removed.
- **Saves.** `/Save` is an IDBFS mount holding only the characters and settings. The C++ side calls `FS.syncfs` after
  writing them (FrontEnd.cpp, NativeSession.cpp).
- **Renderer.** `GlBackend.cpp` asks SDL for OpenGL ES 3, which Emscripten maps to WebGL 2.
- **Main loop.** `emscripten_set_main_loop` runs one frame at a time. Asyncify cannot be combined with wasm64 and native
  wasm exceptions.
- **Threads.** The build is single-threaded, so music is decoded on the game thread. The servers already send the
  COOP/COEP headers that a threaded build would need.
- **Not available in the browser.**
  - TCP/IP games: the browser has no raw TCP.
  - Cinematics: FFmpeg is not in the wasm build.
- **Developer shortcut.** When the project's `MPQ/` folder is next to the server, `Play-Web.cmd` and `serve.js` also
  serve it under `/MPQ/`. The page then offers "use the MPQs on this server", and `?autostart=server` starts with them
  straight away.
