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
- Switch (v1.2): `touch_controls` defaults to 1 on Switch as well, so the handheld touch screen uses the same
  `TouchControls` as Android (sticks, HUD taps, menus as mouse + tap = Space). The old Switch-only path
  (`switchTouch`: menus only) remains for `touch_controls = 0`. The pause button is drawn only in handheld mode
  (`appletGetOperationMode`). The .nro metadata (nacptool) carries the author and version 1.2.0.

## v1.3 fixes
- HUD hearts (>12): the game recolors every heart each tick with `colorit` (`new Transform(mc).colorTransform =`), then
  `hud.lif.gotoAndStop(2|3)` for the 24/48-heart layout, whose PlaceObject moves/shrinks h0..h11. We flagged the
  colorTransform setter as "transformed by script" (as Ruffle does), which froze h0..h2's matrix: big hearts on top of
  the small ones. The original Flash Player re-lays them out, so `Transform.colorTransform` now only sets
  `scriptColor` (blocks the timeline's color, not its matrix). Determinism runs unchanged.
- Tears: `create()` attaches the ball at (0,0) after the tick's movement code (`actions1` runs before `playc`); the
  next tick moves it to Isaac and sets `d._y = dy` (-23, the tear's height). The renderer snapped the parent's
  teleport but interpolated the child `d` from 0 to -23: the tear appeared low and rose. Now a teleported node's
  subtree snaps with it (`NodeInterp::jumped`), and a node created at (0,0) in the previous tick that then moves is
  treated as a teleport (`Snapshot::fresh`).
- Stuck shooting: SDL controllers shared one `padKeys` map between the left stick (WASD in game, arrows in menus),
  the right stick, buttons and triggers, and the left stick only re-mapped when it moved. Holding it into the boss VS
  screen turned WASD into an arrow that stayed pressed once the fight began. Now every source goes through
  `keySrc(src, code, down)` (a key is down while any source holds it); the left stick and held D-pad directions are
  re-mapped when gameplay/menu changes (also on Switch for D-pad/StickL buttons). Focus loss / background clears the
  mask and the controller state. `ISAAC_TOUCH_INJECT` gained `axis <n> <value>` and `button <n> <0|1>`.
- Android audio: SDL 2.30's `aaudio_PlayDevice` writes with a 1 ms timeout and drops the rest of the block. With the
  low-latency buffer full most of the time, most of each 512-frame block was lost (the "motor" sound; screen
  recording re-routes the stream to a path with other timing, which hid it). CMake now patches it to write the whole
  block (50 ms timeout, up to 4 tries; blocking is what paces SDL's audio thread) and to grow the buffer by one burst
  on each new underrun (Oboe-style latency tuning). If re-opening the stream after an output change failed, SDL's next
  write used a NULL stream and crashed (the app restarted): it now waits and retries every second. The manifest also
  handles more configuration changes itself (density, font scale, touchscreen, color mode).
- Touch HUD targets: on a wide phone the bomb row is ~2 mm tall and a tap just above/below it spawned the movement
  stick. Targets are now tested in window pixels, grown to at least 13% of the screen height around their center,
  nearest center wins.

## v1.3 features: controller menus, rebinding, save converter
- Controller navigation (`rt/menuui.cpp`, logic thread). The game's menus are SWF buttons for the mouse. In menus /
  the pause screen the main thread turns D-pad / left stick / A / B into InputEv type 5 (`NAV_*`, auto-repeat 0.38 s
  then 0.11 s); `padNavEvent` keeps a focused button among the visible, enabled, on-stage ones of the topmost
  `_root` child that has any (options sheet over the main menu, ...), moves it to the nearest button in the pressed
  direction (distance along + 2.2 x sideways), hovers it with a mouse move (the game's own rollover art) and clicks it
  with a synthetic mouse down/up at a point inside its hit shape. B: `_root.paus` on frame 2 (the pause screen) ->
  P (resume), else Escape (the game's Back buttons have `keyPress "<Escape>"`). Screens without buttons get arrow /
  Space / Escape pulses (3 ticks). The snapshot carries the uid of what turns red: the button, or for buttons that
  are only an invisible hit area (the challenges `pauss.cN`, the collection's `col.iN`) the clip around them; their
  rect is the hit area (`hitStageBounds`). The renderer marks every leaf of that subtree with alpha-add = (10 +
  brightness of the parents) * 255, and the fragment shader's "ink" mode turns pixels darker than paper into one fixed
  red, making translucent ink opaque (some labels are black at ~35% alpha) so every option has the same red. Once the
  parents are darker than 0.97 (the game's `darky` fade to black is a colorTransform on `_root`) it isn't shown: the
  old additive tint stayed red over the fade. Direction choice: candidates inside a 45-degree cone or on the same line
  first (distance between facing edges + perpendicular gap), then anything ahead (within 2:1 for up/down; any for
  left/right, e.g. Collection -> Exit Game). Default selection when a screen opens or the selected option disappears:
  Start (first button of `_root.men.men`) when it is on screen; in the paper sheets (`pauss`, `hud.paus`) the topmost
  option that isn't a Back button (key condition 19 = Escape); elsewhere the one nearest the last selection. While the
  candidates' parents move (the main menu camera, sheets sliding in; capped at 45 ticks) nothing is shown or chosen.
  The mouse over a button (or the rebinding rows) sets `Snapshot::handCursor` -> SDL hand cursor (PC only).
- PC audio: the default output device at start-up is opened by name (`SDL_GetDefaultAudioInfo`), so a controller with
  audio that becomes the system default (DualSense on Windows) doesn't take the sound. On SDL_AUDIODEVICEADDED/REMOVED
  the default is re-checked after 0.5 and 2.5 s: the game moves to it if it isn't a controller ("controller",
  "dualsense", "xbox"... in its name) or if its own device was lost. Android / Switch keep SDL's default device. Mouse/touch/keyboard input sends NAV_OFF (outline hidden). Held directions change meaning when the
  game switches between gameplay and a menu (re-evaluated every frame). Headless: `ISAAC_MENUUI=1` + `eval nav N`.
- "REBIND CONTROLS" entry: `_root.paus` (pauss in menus, hud.paus in a run) frame 8 is "Control options" in the
  three games (same layout); a synthesized text field is placed in it at script depth (local 64,186), removed on
  other frames. Click/tap (`menuMouse`) or controller confirm -> `g_rebindRequests` -> snapshot -> main thread opens
  the screen.
- Synthesized characters: `rectShape` / `textField` build ShapeDef / EditTextDef at run time (ids from 0xfffe down,
  cached by parameters) and `instantiate` them, so the rebinding screen is a normal part of the display list,
  drawn in the game's HUD font (Upheaval, found by name) and scaled with `_root`. `g_synthCharBase` keeps them out of
  the learned preload lists.
- Rebinding screen: state on the main thread (`RebindScreen`, `rt/controls.cpp`), which publishes a `UiModel`; the
  logic thread rebuilds `_root.__controls` when its version changes and returns the row rects (stage) for taps.
  While it is open no input reaches the game. Capture takes the next key / controller button (triggers = 100/101);
  assigning an input already used by another action swaps them. Closing saves isaac.ini.
- Bindings: actions -> keys (SDL keycodes) / controller inputs (SDL GameController numbering everywhere; Switch
  buttons mapped by position). A device that was never customized keeps the original behaviour exactly; once
  customized, its inputs are translated in gameplay (and Pause in the pause screen) to the key the game reads for that
  action given `so.data.frog` / `so.data.left` (`gameCode`), and original action keys that are no longer bound do
  nothing in gameplay. Controller: `controller_move_stick` swaps stick roles. Touch: side and joystick size.
- Save converter (`tools/save_converter.html`, shipped in the packages as `save_converter.html`): ISAACSO1 <-> Flash
  LSO (AMF0). Keeps every field both ways except the Flash-only `wind`/`res` (not written to IsaacRecomp); writes
  `cocks = true` to Flash saves (otherwise the Flash game restores stats from its serial.txt backup); empty cols/boss
  slots become 0 in Flash and undefined in IsaacRecomp, like each game writes them. Tested both ways and round trips
  on saves of the three versions.

## v1.3 follow-ups
- "Rebind controls" label: written with hand-drawn stroke glyphs (`rt/menuui_glyphs.inc`, original artwork; small
  caps like the game's marker labels) as one synthesized line shape, cap height 26 at (74,155) in the options sheet.
  The game's own menu labels are plain shapes (no font to reuse).
- The Hourglass: slowed enemies advance their animation only on even ticks (`trgnextd` skips `nextFrame` when
  `trg.free && slow > 0 && !fra2`), while attacks fire on `trg.d.d._currentframe == N`, so each attack frame was seen
  on two ticks. `trgnextd` is wrapped natively: on a held tick the clip gets `holdTick`, and `_currentframe` reads
  N + 0.5 for the rest of that tick (equality and end-of-animation checks fail, range checks still pass).
- Touch: a gameplay <-> menu switch no longer drops the joysticks; their keys are released and re-pressed from the
  finger's last position when play resumes (`Stick::fx/fy`). Joystick type 2: the base is dragged so the finger is
  never farther than the radius.
- Touch taps: whether a tap may become Space is decided by the screen at finger-down (`tapOnButtons`): the tap's own
  click could change the screen first (Credits button -> credits, which a Space skips).

## v1.3.1
- Controller focus colour: the shader's ink mode has two targets, selected by the alpha-add marker: 10 + fade =
  pastel pink (0.78, 0.53, 0.53), 20 + fade = deep red, used only for the main menu's Start (`Snapshot::focusStrong`).
- Navigation: the "same line" test uses the middle half of each rect's perpendicular span. The collection's hit areas
  (70 px, 53 px apart, grid tilted ~5 degrees) overlap their neighbours, so with full spans the item to the left
  counted as "below" and moves went sideways. Score adds 0.3 x the centers' perpendicular offset. Collection opens on
  `i0`.
- Dialogs: a button whose hit area covers >= 80% of the stage (the exit note `men.ex`: "No" is everywhere outside the
  note; also the sheets' Back buttons) restricts the candidates to the clip that contains it; otherwise the exit note
  shared the `men` layer with the main menu and the selection stayed behind it.
- PC saves: `<program folder>/saves/` (was SDL_GetPrefPath; the old files are copied once, never deleted; if the
  folder can't be created, the old location is used). Windows file names are UTF-8 everywhere: the exe carries a
  manifest with `activeCodePage = UTF-8` (`windows/isaac.rc`; Windows 10 1903+), and the save goes through
  `platformFopen` / `platformReplaceFile` (`_wfopen`, `MoveFileExW`) so it works on older Windows too. The restart after
  "Delete save" uses the wide APIs. Before, a user folder with non-ANSI characters made the save unreadable/unwritable.
- Navigation scoring (replaces the above): sideways = distance from the current option's center to the other one's
  span (0 if the center is in front of it); pass 0 needs sideways <= along (centers); score = facing-edge gap +
  0.3 along + sideways + 0.2 center offset. Fixes Fullscreen -> Controls (skipping Music) and Back -> Lefty Flip
  (skipping the wide Rebind label).
- Not options: buttons without any action script (the pause's "Options" left under the in-game exit note on frame 6),
  the pause screen's item icons (`hud.paus.iN`), and, when a full-screen-hit button exists, buttons the mouse can't
  reach (`pointOn`: center or a 5x5 grid must hit the button itself).
- B: on the pause screen itself P; else the dialog's full-screen button ("No"), else a visible Back button (key cond
  19), clicked like the mouse; Escape only when there is none (Escape closed every sheet: Controls -> main menu).
  Default in a dialog: its full-screen button.
- While the screen moves, the current selection stays shown if it still exists; after a press that sets the screen
  moving (`g_afterClick`, 3 ticks) nothing is shown until it settles (the old option used to slide away with the
  camera and the new default appeared after it, which looked like flicker).
- Character select "Random" (`rander`) only acts when more than one character is unlocked (original behaviour).
- PC audio at start-up: if the default output is a controller, the best other output is opened (names with
  speakers / headphones in several languages first; HDMI, digital and virtual outputs last).
- Touch bomb target: `hud.cacher` exists only in Eternal; the original and WotL have the coin/bomb/key icons directly
  in `hud` at the same place, so the target comes from the same HUD-local rect (249.8,11.1)-(279.4,66.3).
