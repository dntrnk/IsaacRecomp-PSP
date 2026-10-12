THE BINDING OF ISAAC — native version (C++ recomp)
==================================================

This is a static recompilation of the Flash game to C++: all of the ActionScript code has
been translated into compiled C++, and Flash Player has been replaced by a custom native
engine (SDL2 + OpenGL 3.3). There is no Flash interpreter and no Ruffle inside.

All three versions of the Flash game available on Steam are supported, each with its own
recompiled code:
  The Binding of Isaac (the original)         495 scripts, 180 functions, 54 buttons
  Wrath of the Lamb                           679 scripts, 225 functions, 62 buttons
  Eternal Edition (the last Flash version)    712 scripts, 245 functions, 62 buttons

WHAT YOU NEED
-------------
- 64-bit Windows (or Linux) with a graphics card that supports OpenGL 3.3.
- The Steam game, in any of its three versions. Its install folder contains two
  executables:
    Isaac.exe             the game itself: it contains all of its data (graphics, sounds,
                          music and code). This is the one you need.
    Binding_of_Isaac.exe  just the Steam launcher; it won't work.
  Graphics, sounds and music are read from Isaac.exe at startup; they aren't included here.
  The program detects which version it is by itself. (An extracted .swf works too.)

INSTALLATION
------------
1. Unzip the folder wherever you like.
2. Copy Isaac.exe from the game's Steam folder next to IsaacRecomp.exe
   (Steam → right-click the game → Manage → Browse local files),
   or set its path in isaac.ini (swf = C:\path\to\Isaac.exe). You can also copy the
   contents of this folder into the Steam folder, next to Isaac.exe.
   An extracted .swf still works.
3. Run IsaacRecomp.exe (on Linux: ./isaac).

SEVERAL VERSIONS AT ONCE
------------------------
Since all three are called Isaac.exe on Steam, copy them into the program's folder under
different names, for example:
    Isaac-Original.exe   Isaac-WotL.exe   Isaac-Eternal.exe
(the name doesn't matter: each version is recognized by its contents). If there is more
than one, a selection screen appears at startup (arrow keys or mouse, Enter to play; with
a controller: D-pad and A). Your choice is remembered (isaac.ini: game = og, wotl or
eternal) and preselected next time. Each version has its own save file.

WHAT'S DIFFERENT FROM THE FLASH VERSION
---------------------------------------
- Native 1080p: vector graphics are rasterized at your screen's real resolution
  (1440p/4K too). The original 4:3 aspect ratio is kept, with bars on the sides.
- 60 fps (or your monitor's refresh rate: 120, 144...): the game logic still runs at
  exactly 30 Hz, so speed, physics, fire rates and enemy patterns are the original ones.
  Each frame is drawn by interpolating between the last two logic ticks: movement,
  scaling, rotation, fades, color changes, morphs and the timeline's tweened animations
  (including room transitions) all play at the refresh rate. In exchange, the image runs
  on average about 20 ms behind the logic (half a tick); set smooth = 0 to go back to the
  original 30 fps.
- The game logic runs on its own thread and drawing on another, with its own clock synced
  to the monitor's refresh. A heavy tick (loading a room, an explosion with lots of
  objects) no longer delays frames.
- Performance: in testing, the game logic took on average less than 1.5 ms per tick (out
  of the 33 ms available) on a modest virtual machine, even with more than 20 enemies on
  screen. New graphics are rasterized in parallel on all cores and placed in a texture
  atlas created at startup.

CONTROLS
--------
Same as the original: WASD to move, arrow keys to shoot, E or Shift for bombs, Space for
the active item, Q for card/pill, P or Esc to pause.
- F11, Alt+Enter or F (during play): toggle fullscreen / window. Also available from
  Options; remembered for next time (isaac.ini).
- F12: screenshot (screenshot_N.bmp in the save folder).
- F3: performance diagnostics (see below).
Controller (XInput / SDL): left stick or D-pad = move; right stick or A/B/X/Y = shoot;
LB or left trigger = bomb; RB or right trigger = active item; right stick click = card/
pill (Q); left stick click = pause (P); Start = Enter; Back = Esc.

THE isaac.ini FILE
------------------
Created automatically next to the executable the first time you run it.
  fullscreen = 1      fullscreen (borderless)
  vsync = 1           vertical sync
  width/height        window size in windowed mode
  volume = 100        master volume (0-100)
  smooth = 1          interpolation at the refresh rate (0 = original 30 fps)
  filters = 1         blur effects in cutscenes
  cache_mb = 768      maximum video memory for rasterized textures
  gpu_sync = 1        waits for each refresh before preparing the next frame
                      (steadier pacing; set it to 0 if things feel worse)
  low_latency = 1     keeps reading keyboard/controller input while waiting for the
                      refresh, so a press reaches the game up to ~8 ms sooner (0 = off)
  interp_lead = 0     experimental, 0 to 0.5: draws that fraction of a tick ahead
                      (less visual delay, but movement can overshoot slightly when you
                      stop or turn). The game logic is never affected.
  game = wotl         last version played (og, wotl or eternal; the old key
                      "juego" is still accepted)

IF YOU NOTICE STUTTERING OR SLOWDOWNS: F3
-----------------------------------------
Press F3 during play. The following appear in the top-left corner, updated every second:
  FPS (HZ)              frames per second and monitor refresh rate
  MAX FRAME             the slowest frame of the last second
  HITCHES               frames that took longer than 1.5 refreshes
  HELD                  frames where the logic hadn't finished its tick yet
  LOGIC / MARGIN        logic time per tick (30 per second) and the margin the renderer
                        leaves it to finish (adjusts itself)
  RENDER                CPU time spent preparing the frame
  GPU/VSYNC WAIT        time spent waiting for the graphics card / the refresh
                        (with vsync it's normal for this to be high: it's idle time)
  RASTER / UPLOAD       new graphics rasterized and MB sent to the GPU
  NEW TEX / GC          textures created (should be 0 while playing) and garbage collector
While F3 is on, "performance.txt" is also written to the save folder (one line per second
and one line per stutter, with its breakdown). If something runs slowly, play the
problem room for a while with F3 on and send that file: it shows exactly which part is
taking the time.

SAVE DATA
---------
The game is saved when you quit (and whenever the game asks for it) in:
  Windows: %APPDATA%\IsaacRecomp\WrathOfTheLamb\
  Linux:   ~/.local/share/IsaacRecomp/WrathOfTheLamb/
with one file per version: isaac_og_save.sol (original), isaac_save.sol (Wrath of the
Lamb) and isaac_eternal_save.sol (Eternal Edition).
The same folder also holds log.txt (program messages from the last session) and
preload.txt / preload_og.txt / preload_eternal.txt (graphics learned on this machine;
older precarga*.txt files are merged in automatically).
Not compatible with Flash save data: unlocks start from scratch.

CHANGES IN VERSION 10 (v1.1.0 public release numbering)
-------------------------------------------------
- Fixed a slowdown that built up during long sessions (and could end in a crash on
  Switch): every sound the game played left an invisible clip behind, because
  Sound.position returned 0 instead of the sound's length once it had finished. After
  20-30 minutes there were thousands of them. All three versions were affected.
- Sound.duration now uses each sound's real sample rate.
- The learned preload list no longer grows without limit, and preloading never takes
  more than ~60% of the texture atlas.
- Eternal Edition: the hard-mode mark no longer shows on the HUD when hard mode is off.
- Fixed damage from blood/creep puddles: the game checked the floor at the wrong
  spot (1.5x-2.25x further from the top-left corner than the player), so puddles far
  away could hurt you and the ones under you sometimes didn't. All three versions.
- Fewer hitches in long sessions: the game now produces ~60% less memory garbage, and
  cleaning it up can no longer make a game tick miss its deadline.
- Lower input latency: input is read continuously while waiting for the display
  (low_latency), plus the experimental interp_lead option.

CHANGES IN VERSION 9
--------------------
- Besides Wrath of the Lamb, the original game and Eternal Edition can now be played (see
  "SEVERAL VERSIONS AT ONCE"). Everything from previous versions (60 fps, 1080p,
  preloading, simplified options menu, controllers...) works the same in all three.
- File names and settings are now in English: log.txt, performance.txt, preload*.txt,
  screenshot_N.bmp and "game =" in isaac.ini (old files and the old key still work).

CHANGES IN VERSION 8
--------------------
- Game data is read directly from Steam's Isaac.exe: there's no need to extract the .swf
  anymore. The Windows program has been renamed IsaacRecomp.exe so it isn't confused
  with it.
- No more loading screen: graphics are preloaded in the background while the intro
  cutscene plays. It can't be skipped until preloading finishes (a few seconds).
- "Delete Save" (in Collection) really deletes the save: the game restarts itself so
  that stats, collection and unlocks go back to zero.
- In windowed mode, the window fits the usable area of the screen (without being covered
  by the taskbar) and its title bar is visible.
- The thin white seams between colored areas of the same drawing are gone (they showed up
  in boss backgrounds, floor transitions...): shapes are rasterized the way Flash does it,
  without the background showing through the joins.

CHANGES IN VERSION 7
--------------------
- Preloading: at startup the game's graphics (HUD, cutscenes, menus, enemies...) are
  rasterized in advance, so gaps or blurry versions no longer appear the first time
  something is shown. The list is included and fills itself in with whatever you see
  (preload.txt in the save folder).
- Room transitions: everything the game draws into its layers (each room's floor and
  decoration, blood, shadows, the minimap and the screen capture that slides when you
  change rooms) is now drawn by the graphics card. Going through a door used to cost the
  logic two ticks of ~35 ms on PC (and hundreds of ms on Switch); now it's under 3 ms.
- If a tick takes too long, the logic carries on from that point instead of catching up
  by running several ticks in a row (which skipped the transition animation).
- Blurs: computed at reduced resolution when they're large, and reused if their content
  doesn't change (the main menu has a dozen of them).
- Streaming music and sounds: fixed the playback position calculation.

CHANGES IN VERSION 6
--------------------
- New graphics (when entering a room, enemies spawning) are rasterized in the background:
  a frame waits a few ms at most and, if something isn't ready, it appears on the next
  frame instead of freezing the image.
- Each frame uploads its vertices in one go to a ring buffer and replays its draw calls
  at the end (previously a fresh 16 MB buffer was allocated every frame and dozens of
  small uploads were made). The F3 overlay is drawn as a single image and shows the frame
  breakdown (adopt, build, draw); performance.txt is written from another thread.
- The garbage collector frees dead objects based on time (a few ms per tick) instead of
  count: the periodic logic hitches are gone.

CHANGES IN VERSION 5
--------------------
- New: Nintendo Switch version (homebrew), in a separate package with its own
  README_SWITCH.txt. The save file is the same on both platforms.
- Fixed a memory leak: every room visited was kept in memory (with its 6.7 MB layers)
  because the game clears its mouse listeners in a way the engine didn't understand.
  After a long run the game could exceed 1 GB; now it stays stable.
- Hit areas of vector graphics are only computed when the game uses them: about 95 MB
  less memory and slightly faster startup.

CHANGES IN VERSION 4
--------------------
- "Exit Game" in the main menu now closes the game (saving first). The Steam version did this
  through its wrapper (Zinc), which doesn't exist here.
- Cleaned-up options menu: "Detail", "FullScreen Resolutions" and "Window size" have been
  removed, since they depended on Flash/Zinc and did nothing; "FullScreen" now actually
  works. On the controls screen, "Game pad?: use JoyToKey (google it!)" has been removed:
  controllers work out of the box.

CHANGES IN VERSION 3
--------------------
- New rendering engine: logic and drawing run on separate threads, communicating through
  scene snapshots (one per tick). The renderer interpolates between the last two at the
  monitor's refresh rate, with a presentation clock that advances exactly one refresh per
  frame, so motion advances in even steps.
  Previously only movement made by the game code was smoothed; now room transitions,
  fades, color flashes, rotations and scaling, and the timeline's tweened animations also
  run at 60 fps.
- Fixed the ~30 ms stutters (about 3 per second) that the diagnostics recorded every time
  a new graphic appeared: creating textures mid-game stalled the video driver. Now
  graphics go into an atlas created at startup, pixels are uploaded through an
  asynchronous buffer (PBO) without waiting, each room's layers reuse the previous room's
  textures, and menu blurs reuse their intermediate textures.

CHANGES IN VERSION 2
--------------------
- Fixed: Isaac's sprite (and those of other characters) would sometimes "come apart" and
  appear shifted downward for an instant while walking. The 60 fps smoothing was also
  extrapolating the jumps in timeline animations (body parts change position from one
  keyframe to the next); now only movement made by the game code is extrapolated, and
  only if it's consistent across ticks.
- Performance with many enemies and when changing rooms:
  * every blood splat or debris sent the whole floor layer (6.7 MB) to the graphics card;
    now only the small piece that changes is sent;
  * the garbage collector paused for ~100 ms every minute or two (more often the more
    enemies there were); now pauses are 1-2 ms;
  * each room's layers (6.7 MB each, in memory and on the GPU) weren't freed until that
    collection and piled up room after room; now they're recycled;
  * the vector rasterizer was up to 3-4 times slower than necessary with complex shapes,
    and ran on a single core; room backgrounds that have already been seen are reused.
    Entering a room cost 70-260 ms in one go; now 20-50 ms the first time and 10-25 ms
    when coming back (measured on a virtual machine with only 2 cores; considerably less
    on a normal PC);
  * vertices are sent to the GPU without sync waits;
  * cutscene blurs reuse their intermediate textures and also look right on 16:9 screens.

KNOWN LIMITATIONS
-----------------
- Frame-by-frame animations (where each frame is a different drawing, e.g. Isaac's legs
  while walking or some particles) have 30 drawings per second in the original, and
  that's how they look: there are no in-between drawings to show. Everything that moves,
  rotates, scales or changes color does run at the refresh rate.
- Tested with automated runs (thousands of frames with random input, menus, cutscenes,
  rooms, items and combat), but not in every possible situation. If you find something
  odd, note down when it happens.

Third-party software: SDL2 (zlib), PlutoVG (MIT), dr_mp3 (public domain / MIT-0),
stb_image (public domain / MIT). See THIRD-PARTY-LICENSES.txt.
