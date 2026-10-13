# The Binding of Isaac (Flash) — native C++ recomp (design notes)

## Multiple versions (v9)
- Three games in one executable: original (`og`), Wrath of the Lamb (`wotl`) and Eternal Edition (`eternal`).
  `tools/gen.py ... --ns NAME` generates each one into `gen/NAME/` (PC, 1440x1080) and `gen720/NAME/` (Switch) inside
  `namespace fl::NAME`, with its own `GameCode g_code_NAME` (script/button tables, stage, atoms).
- swfload.cpp: `kGames` (decompressed SWF size + CRC-32); Movie::load identifies the game and sets `g_code`
  before walking the timeline. `peekSwfLength` reads only the beginning of an .exe/.swf (selector).
- Zinc: XOR key a + 0x21*i with a different `a` per executable (0xd5 og, 0x83 wotl, 0xa1 eternal): deduced from
  "CWS"/"FWS".
- The HD patch (patchhd.py) works as-is for all three. Eternal only takes the Zinc path of `fullf` if
  `mdm.System.macVerString` exists (otherwise "nozinc" mode with layers at 4x): builtins.cpp defines it.
- Switch: on exit ALWAYS call platformStopThreads() (hbloader loads hbmenu and the next .nro in the same process).
- Switch: pacing with fences (renderPaceAfterSwap/renderPaceWait, `low_latency`): at most one frame queued on the
  GPU; previously the driver allowed more to queue and the stall showed up inside "draw" (~16 ms), with more input lag.
- patches.cpp: per-game ids (`kIds`) for the options menu and the controls screen.
- main.cpp: finds every .exe/.swf in the folder, selector (`chooseGame` + `renderChooser`) if there are several,
  `game =` in isaac.ini (old `juego =` still accepted), `--game` (tests and restart after "Delete save"); save and
  preload list per game.
- Regenerate: FFDec `-export script`, patchhd.py on `frame_1/DoAction.as` (2.25 and 1.5), `-importScript`,
  `patchhd.py --bin`, `gen.py --ns`.

## Pipeline
1. `isaac.swf` (original) → `tools/patch1080.py` (+ FFDec importScript) → `isaac1080.swf`
   (1080p patch: _root at 225 %, 1440x1080 stage, high-resolution BitmapData).
2. `tools/gen.py isaac1080.swf gen/` translates ALL of the AVM1 bytecode (frame DoActions, functions,
   button actions) to C++ (`gen/*.cpp`). No interpreter: each block is a C++ function.
3. The executable loads the assets (shapes, sprites, sounds, fonts) from the user's ORIGINAL SWF at
   runtime (no assets are redistributed). Actions are linked by
   (sprite_id, frame, index_in_frame) and (button_id, condition_index).

## Semantics (ported from Ruffle: https://github.com/ruffle-rs/ruffle)
- SWF8: case-sensitive, undefined→NaN, _x… properties case-insensitive.
- Scopes: Local → Target(clip) → Global. `set` without var: local if it exists, otherwise on the Target clip.
- get on DisplayObject: own props → (_root/_parent/_global/_levelN) → children by name → _x… props → proto.
- for-in: proto keys first, then own keys in REVERSE insertion order, then children (by descending depth).
- AVM1 frame: execution list (newest clip first). For each clip: enqueue onEnterFrame
  (or onLoad the first time); if playing: advance the frame (place/remove immediate, DoAction enqueued).
  Then the action queue is drained (FIFO). instantiate_child immediately runs the child's 1st frame.
  goto: rewind with PlaceObject merging by depth (see run_goto in Ruffle).
- Script depths = AS depth + 16384.

## Threads and rendering
- Logic (own thread, `main.cpp`): 30 Hz ticks on a fixed clock (T_n = base + n/30). At the end of each tick
  `buildSnapshot()` (snapshot.cpp) copies the display list into a flat preorder tree (`SnapNode`:
  uid, local matrix, cx, content, masks, filters) + BitmapData pixel uploads (dirty rectangle)
  + freed bitmaps. Input (keyboard, mouse, controller) arrives through a queue from the main thread.
- Render (main thread): presentation clock that advances one refresh period per frame (with
  vsync + glFinish to measure it accurately), simulation time = presentation − margin (adaptive, p90 of the
  logic's publish delay + 2 ms). Adopts snapshots in order (applying their uploads) and
  draws lerp(previous, current, α). Matched by uid; no interpolation (snap) if the object is new,
  changes graphic/visibility, flips, changes scale/rotation a lot, or jumps >250 px without having been thrown.
  Matrices: scale and angle handled separately. If the logic is late, extrapolates up to α = 1.35.
- The renderer never touches live objects: it rasterizes from `RasterSrc` (def + ratio + copied text).
- Shapes → PlutoVG in parallel (thread pool per calling thread: raster.cpp) → atlas of 2048² pages
  created at startup (shelf packing; the least-used page is evicted). No mipmaps (scale quantized to
  quarter octaves). Uploads through a PBO ring (no waits). BitmapData textures per uid with size
  pooling; bitmaps that stop being visible return their texture after 2 ticks.
- Per-frame commands: leaves, bitmaps, masks (stencil) and blur groups (FBO, reusable best-fit
  intermediate textures).
- BitmapData.draw (logic thread): two phases — plan leaves (shape + final matrix, 1/64 subpixel),
  rasterize those missing from the leaf cache (in parallel, large ones in strips) and composite in order
  (premultiplied source-over). Whatever is under a mask is painted directly. Exact dirty rectangle.
- PlutoVG: per-thread persistent cell memory; plutovg_texture_smooth per thread.
- GC: marking from roots + pinned objects (gcPin), lazy sweep; also triggered by external memory
  (pixels). Pixel buffers are recycled.

## Status (first release)
- Recompiled: 679 frame scripts, 225 functions, 62 button actions (47 distinct AVM1 opcodes, all supported).
- Verified: intro, cutscenes with streaming audio, menus, character select, rooms, transitions,
  items, combat, damage, saving. 3×20,000 ticks with random input and no failures. Logic ≈1.5 ms/tick.
- Compared against Ruffle (same patched SWF): same composition on the title and character select screens.

## Rebuilding
    python3 tools/patch1080.py patch1080/scripts/frame_1/DoAction.as   # on top of the FFDec decomp
    java -jar ffdec-cli.jar -importScript isaac.swf s1.swf patch1080/scripts
    python3 tools/patch1080.py --bin s1.swf isaac1080.swf
    python3 tools/gen.py isaac1080.swf gen
    cmake -G Ninja -B build && ninja -C build
    # Windows: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw64.cmake -DCMAKE_PREFIX_PATH=<SDL2-mingw>/x86_64-w64-mingw32 ...
  `gen/` ships pre-generated, so CMake is all you need.

## Automated tests (headless)
    isaac isaac.swf --headless [--norender] --frames N --script script.txt --monkey SEED --wav out.wav [--stats] [--perf]
  script: "<tick> down|up <keycode>", "<tick> shot file.ppm", "<tick> shotf <frac> file.ppm",
  "<tick> eval _root.a.player._x", "<tick> eval tree _root 2", "<tick> eval set <path> <value>",
  "<tick> eval call <path> <method> [args]", "<tick> eval dumpbd <BitmapData path> file.raw", "<tick> quit".
  --stats: logic/render times per tick and collections. --perf: F3 overlay enabled.
  Environment variables: ISAAC_THREADS=n (extra raster threads), ISAAC_DRAWPROF=1 (BitmapData.draw
  profile), ISAAC_LEAFPROF=1 (slow leaves), ISAAC_NOLEAF=1 (draw() without cache, for comparison).

## Switch (homebrew, .nro)
- Toolchain: devkitA64 + libnx + switch-sdl2 + switch-mesa (nouveau, desktop OpenGL via EGL).
  With devkitPro's pacman, just install `switch-dev switch-sdl2 switch-mesa switch-libdrm_nouveau`.
  Without it, `switch/build-devkit.sh` builds everything from source (gcc 16.1, binutils 2.46, newlib,
  libnx, libdrm_nouveau, mesa 20.1 switch branch, SDL 2.28.5), with the libraries built -fPIC (.nro files are PIE).
- Build (DEVKITPRO must be in the environment: libnx's specs read it):
      export DEVKITPRO=/opt/devkitpro
      cmake -G Ninja -B build-switch -DCMAKE_TOOLCHAIN_FILE=cmake/switch.cmake && ninja -C build-switch
  → build-switch/isaac.nro (nacptool + elf2nro with switch/icon.jpg in POST_BUILD; nacp author "native recomp").
- 720 variant (`gen720/`): 960x720 stage with `_root` at 150 % (on PC 1440x1080 at 225 %), which
  cuts the game's fullscreen BitmapData to less than half. Generated the same way as `gen/`:
      python3 tools/patchhd.py --scale 1.5 patch720/scripts/frame_1/DoAction.as   # copy of the FFDec decomp
      java -jar ffdec-cli.jar -importScript isaac.swf s720.swf patch720/scripts
      python3 tools/patchhd.py --scale 1.5 --bin s720.swf isaac720.swf
      python3 tools/gen.py isaac720.swf gen720
  CMake uses gen720 on Switch (ISAAC_GEN_DIR). With --scale 2.25 patchhd.py reproduces patch1080.
- rt/platform.h: per-platform memory limits (GC threshold, external memory, pixel pool, leaf
  cache, render cache, PBO, atlas pages, raster threads). Building on x86 with
  `-DCMAKE_CXX_FLAGS=-DFL_SWITCH_MEMORY -DISAAC_GEN_DIR=gen720` applies the Switch limits so they can be measured on PC
  (build-swmem: peak ≈ 254 MB RSS while playing).
- Threads: logic on core 1 (16 MB stack), 2 rasterizers on cores 2 and 1, render on core 0
  (core 3 belongs to the system).
- Render (v6): recorded frame (GOp: draw calls + state changes in std::function) replayed in
  renderEndFrame with a single VBO mapping; VBO/PBO rings with fences (StreamRing), no glBufferData per
  frame; asynchronous rasterization (RJob, queue + service threads; the frame waits up to rasterWaitMs while helping);
  no glFinish on Switch (gpu_sync=0). Threads: logic core 1 (0x2C); raster service core 2 (0x2D) and
  0 (0x3C); logic helpers core 2 (0x3B) and 0 (0x3D). Horizon doesn't time-slice between threads of equal
  priority: a render thread must not share a core and priority with long-running logic work.
- v7: "GPU" BitmapData (display.h: displayed/gpu/cpuRead/lazy): layers attached with attachBitmap and never
  read receive draw/fillRect/copyPixels as GpuOps (snapshot.cpp) that the renderer runs in order into an FBO on
  the layer's texture (render.cpp: runGpuOps/renderScene, uFlip = -1). The logic no longer rasterizes or composites; a
  new BitmapData doesn't allocate pixels until the CPU needs them (ensurePx). Reading a GPU layer moves it back to
  the CPU with a warning in log.txt (doesn't happen in this game: only grid/grid2/tests/org are read).
- v7: preloading (PreKey: id, type, variant, scale relative to screen height or absolute for layers): built-in lists
  rt/preload_<game>.inc (tools/preload_list.py merges the ISAAC_LEARN_OUT lists) + learned preload*.txt
  (old precarga*.txt files are merged in once).
- v7: filter cache by uid + content signature; blur with downsampling (1/2 if radius >= 8, 1/4 if >= 24).
- v8: game data from Steam's Isaac.exe (MDM Zinc projector, UPX): after the PE image comes a container
  (header 14 09 13 49 49 13 09 14) holding the SWF as CWS (zlib level 6), preceded by its u32 length and encrypted with
  XOR k(i) = 0x83 + 0x21*i. swfload.cpp: swfFromExe (also Adobe projectors and unencrypted SWFs). Verified:
  the decrypted SWF is byte-for-byte identical to the v1.48 .swf (md5 055f7891...). Binding_of_Isaac.exe is the launcher.
- v8: asynchronous preloading (renderPreload enqueues into g_rqPreQueue; renderPreloadPending advances it each frame) during
  the intro cutscene, with input blocked (pushIn drops keys/mouse) until it finishes.
- v8: seams between adjacent fills: on empty surfaces (textures, leaves) paintShape paints front to
  back with PLUTOVG_OPERATOR_SATURATE (added to third_party/plutovg: dst += src * min(coverage, 1 - dst.a)).
  In the interior it's equivalent to reversed SRC_OVER; on shared edges it leaves the pixel opaque, like Flash.
- v8: mdm.FileSystem.deleteFile("serial.txt") (Delete Save) → g_restartRequest; main relaunches the executable
  (Switch: envSetNextLoad; Windows: CreateProcess; Linux: execv). On Switch hbloader loads the .nro again in the
  SAME process: all helper threads must be stopped first (platformStopThreads; the loops check
  platformStopping()), or the previous program's threads stay alive and corrupt the new one's heap.
- Memory limits (g_lim) chosen at startup: Switch in application mode (>1.5 GB) uses the PC ones.
- Touch: SDL_FINGER* → stage mouse outside of gameplay (the game's on(release) buttons).
- Input: libnx pad → game keys with three mappings (gameplay / pause / menus) depending on the snapshot.
  L3+R3 = F3. Everything (swf, ini, save, log.txt) lives in sdmc:/switch/isaac/.
- Tests: cmake/aarch64-linux-test.cmake builds for aarch64-linux (static) and runs under qemu:
  same deterministic result as x86 (--monkey). Also tested on real hardware.
- Fixes that came out of the port and benefit PC: Mouse.removeListener() with no arguments (it cleared the
  listeners and kept each room's clip alive: ~1 GB after a long run) and shape collision polygons
  computed only when needed (−95 MB).

## Towards PSP
- Platform code isolated in main.cpp (SDL2: window, input, audio) and render.cpp (OpenGL 3.3).
- PSP: needs another render backend (sceGu) behind the same interface (textured quads with
  color transform + stencil), rasterizing at ~0.5 scale (480x272), a texture atlas in the 2 MB of VRAM,
  and not loading the whole SWF into RAM (preprocess into a package with streamed sounds).
- The generated code and the AVM1 runtime are portable C++17 with no platform dependencies.

## Input latency (v1.1)
- Input is read continuously: `pollInput()` runs at the start of each frame and also every ~1 ms while the main
  thread waits (Switch: GPU fence wait; PC: until 3 ms before the expected vblank, then glFinish; no vsync: the frame
  limiter). An input no longer waits for the next frame, and no longer slips to the following logic tick when a tick
  boundary falls inside that wait (model: ~8 ms less on average from press to first visible movement).
- `interp_lead` (0..0.5, default 0): simT += lead·dt, i.e. optional partial extrapolation of the interpolated view.
  Rendering only; the logic is untouched.

## Sound clip leak (v1.1)
- All three games remove each sound's `soundz` clip when `s.duration == s.position`. `position` returned 0 once the
  channel had finished, so every sound leaked a clip under _root (~4000 after 30 min: logic 30 ms/tick, GC 150 ms/s,
  eventually bad_alloc on Switch). Now SoundObj keeps the last position, and a sound that played to the end reports
  position == duration (Flash behaviour). `duration` uses the SWF rate field (5.5/11/22/44 kHz).
- Preload: keys above q100 = 800 (4x) are neither learned nor preloaded; learning dedupes by scale bucket; preloading
  stops at ~60% of the atlas (estimated from content bounds).

## Creep / blood damage (v1.1)
- All three games test floor creep with `blorz.hitTest(x + _X, y + _Y, true)` (`shit()`), and enemies on hazards
  with `dang2.hitTest(xp, yp, true)`. Flash takes that point in **_root coordinates** (the docs say "Stage"; Ruffle
  does the same: `avm1_root().local_to_global`). We treated it as stage coordinates, and since _root is scaled
  (x2.25 PC, x1.5 Switch), the test landed 1.5-2.25x away from the player: blood far away hurt, blood underneath
  didn't. `mc_hitTest` now maps the point through `_root`'s matrix. Repro: `eval call _root.a parc bloo 300 300`
  then `eval call _root.a shit 300 300` (true) / `shit 675 675` (false; it was true before).

## GC hitches (v1.1)
- Switch perf logs after ~30 min: phases of 30-90 s with GC 200-650 ms/s (lazy sweep at its 18 ms/tick ceiling),
  logic 15-38 ms/tick, "logic delayed" hitches, and 20-70 ms render-thread `adopt` spikes at the same moments
  (snapshot frees competing for the malloc lock).
- ~70% of the garbage was short strings rebuilt every tick ("m12-3.mp3", "hat2", numbers as text). `mkstr` now has
  a weak 8192-entry cache for strings <= 48 chars (Strs are immutable; entries not marked are dropped right after
  each mark). Garbage per tick in Caves: 117-254 -> 49-75 objects. Headless runs stay bit-identical.
- `gcCollect(tickMs)`: the sweep never takes the tick past 12 ms (minimum gcSweepMs, so it always progresses), and a
  mark is postponed on a heavy tick unless the heap is 1.5x over the trigger.
- Replaced snapshots are handed back to the logic thread (`renderFreeRetired`) instead of being freed on the render
  thread.
- Diagnostics: `ISAAC_GCHIST=1` (garbage by type at each collection), `ISAAC_STRPROF=1` (dynamic strings created, by
  pattern), `--stats` prints the total GC time.

## Android (v1.2)
- Same program built as `libmain.so` (SDL 2.30.9's Java activity, fetched by CMake from GitHub) with `gen720/`;
  arm64-v8a, minSdk 24. Gradle project in `android/`; `.github/workflows/android.yml` builds the APK in CI (Google's
  SDK hosts are not reachable from the development sandbox). See `android/README.md`.
- Renderer: `g_glES` → `#version 300 es` shaders and RGBA8 textures. ES has no BGRA upload format, so `texUpload`
  swaps R/B while copying into the PBO (same cost as the memcpy). Output is pixel-identical to desktop GL (max
  difference 2/255 on Mesa). `ISAAC_GLES=1` tests it on PC.
- Data: `/storage/emulated/0/IsaacRecomp/` (from `IsaacActivity.rootDir()` via JNI) holds Isaac.exe, isaac.ini,
  saves, log.txt, performance.txt. Needs MANAGE_EXTERNAL_STORAGE (Android 11+) / storage permission (older): native
  `main` waits (pumping events) until `hasStorageAccess()`. Process is `exit(0)`ed at the end (Android may reuse it).
  "Delete save" restarts through `RestartActivity` (separate process). Background: P pressed if playing, logic
  thread frozen (`logicPaused`) until foreground.
- Touch (`rt/touch.cpp`, `TouchControls`, main thread): in gameplay (snapshot `gameplay`) a finger on the left half
  spawns the movement stick (WASD, 8 sectors), on the right half the shooting stick (arrows, dominant axis with
  hysteresis); floating base; dead zone 18%. HUD targets come from the logic thread each tick
  (`recompTouchTargets`: `hud.cacher` middle row = bombs/E, `hud.it` = Space, `hud.extra` when visible = Q; same
  layout in the three games) and win over the sticks. Keys are held at least 90 ms (game polls at 30 Hz).
  Menus: the finger is the mouse; a short tap also sends an InputEv type 4: the logic thread presses Space for 3
  ticks if there is no button under it and no button on screen (`touchButtonAt` / `touchAnyButton`) - skips cutscenes,
  title cards. Back key: P in game (pause/unpause), Esc in menus (title: the game's quit dialog).
- Controllers (all SDL platforms): context-aware mapping like Switch (gameplay / pause / menu): A confirms and B goes
  back in menus; Start/Back pause in game.
- First device test (a mid-range phone): menu 18-25 fps (17 blurs on the title
  screen; each recomputed blur = FBO switches, very expensive on tiled GPUs), gameplay ~50 fps with many HELD
  frames, preload 34 s, and a tap skipped the intro during the preload (type 4 was not blocked). Changes:
  * render resolution: `IsaacActivity.setRenderHeight` → `SurfaceHolder.setFixedSize` (the display hardware scales
    for free). `IsaacSurface` re-normalizes touches by the view size (SDL divides by the buffer size). Default 720
    lines; automatic mode steps down 720→600→540→480 when gameplay stays <88% of the refresh for 10 s with the time
    in render/GPU wait (not HELD), stored as `auto_height`. `render_height` forces a value.
  * `gpu_sync` off on Android (glFinish after swap serialized CPU and GPU): fence pacing as on Switch.
  * blurs on mobile (`filterBudget` 4/frame, `filterMaxAge` 3, `filterLowRes`): changed blurs beyond the budget
    show their previous result (refreshed at least every 3 frames); smaller RTs. Overlay shows `BLURS (REDONE)`
    and upload time.
  * threads: logic and render pinned to the fastest cores (cpuinfo_max_freq), helpers to the others
    (`platformSetThread`: prio 0x2C = main roles).
  * taps blocked while preloading; touching shows "LOADING n%" (`renderHint`). `_exit` at the end (exit ran static
    destructors under live helper threads: FORTIFY "destroyed mutex"). Saves flushed on the logic thread when
    the app goes to the background.
  * joysticks: base fixed (radius back to 12% of the screen height, as in the first build) (knob clamps at the edge). Pause button right of
    "-LIFE-" (root rect 588,4-616,32; touch area +40%), drawn by `renderPauseButton`.
- Testing on PC: `ISAAC_TOUCH=1`, `ISAAC_TOUCH_INJECT=file` (synthetic touches/back/eval/screenshots in the threaded
  loop), `ISAAC_INPUT_LOG=1`. Under Xvfb without an audio device the opening cutscene can end by itself early; from
  there the title's Start did not react once (not seen when the intro is skipped with a tap).
- Second device test: performance fine. The picture filled only the top-left 2/3 of the screen: SDL adds its
  SurfaceView without layout params, so after `setFixedSize` the view measured itself to the buffer size; now it
  gets MATCH_PARENT (the buffer is then scaled to the screen). Also the activity
  forces immersive mode itself (`goImmersive`, on focus/resume; cutout short edges). Audio sounded worse than on PC:
  we mixed at 44.1 kHz and SDL resampled again to the device's 48 kHz; now the device is opened with
  `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE` (48 kHz requested on Android) and we mix at its rate, with Catmull-Rom
  interpolation instead of linear (3 samples of history across refills; channel end timing and Sound.position
  unchanged, headless runs stay identical).
- Third device test (input and sound lag). In gameplay DRAW was ~14 ms per frame with ~20 quads: the driver waits
  for a free window buffer at the frame's first draw, after input was read. `renderPaceAfterSwap` now clears and
  flushes the default framebuffer right after the swap on Android, so that wait happens before the next frame polls
  input (same frame rate). Texture uploads go straight from memory on Android (the PBO path stalled: 0.2 MB/s cost
  up to 100 ms/s). Audio: 512-sample buffer on Android, and CMake patches SDL 2.30's AAudio backend to request
  `AAUDIO_PERFORMANCE_MODE_LOW_LATENCY` (SDL leaves that call unused; without it Android picks large buffers).
- Fourth device test (a 120 Hz tablet). Audio sometimes started as a rough "motor" buzz that went away when another
  app opened an audio stream (screen recording): continuous underruns. With LOW_LATENCY the AAudio stream's buffer
  size defaults to one burst, smaller than SDL's 512-frame write. The CMake patch now also enables
  `AAudioStream_setBufferSizeInFrames`/`getBufferCapacityInFrames` and sets the buffer to 2x the SDL block after
  open/reopen (capped by the capacity). Input lag: render 1-3 ms, many HELD frames at 120 Hz; what remains is the
  30 Hz game tick plus interpolation (up to ~1 tick); `interp_lead` trades part of it for slight overshoot.
- Analog movement (`joystick_acceleration`, default 1; all platforms with a stick): the game only knows 8 digital
  directions, so a native wrapper around `_root.a.dirkey` (installed by `installMoveScale`, re-wrapped if the game
  redefines it) scales the player's `xenf`/`yenf` by `g_moveScale` after the original runs. The main thread sets
  `g_moveScale` from the movement stick each frame: `moveCurve` = 0 inside the dead zone (touch 18%, controller
  20%), then 40% rising linearly to 100% at 75% of the stick's travel, never above the original speed. Keyboard,
  D-pad and `joystick_acceleration = 0` keep scale 1 (headless runs and determinism unaffected). Switch: in
  gameplay the left stick position is read directly (`padGetStickPos`) instead of the digital StickL buttons.
  Measured: scale 1 / 0.7 / 0.4 = 211 / 148 / 85 px/s.
