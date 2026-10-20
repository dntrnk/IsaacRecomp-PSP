# Differential tests against Ruffle

The recomp is compared with [Ruffle](https://github.com/ruffle-rs/ruffle) (an independent AVM1 implementation)
running the **same HD-patched SWF**, tick by tick, with the same input, the same random numbers and the same clock.
Any difference in the game state (enemies, the player, tears, the room) points to a runtime bug in one of the two.

## Building the Ruffle harness

    git clone https://github.com/ruffle-rs/ruffle && cd ruffle
    git checkout 03da47e244ba9f616cc405b196d20fb61f42262d
    git apply /path/to/isaac-recomp/tools/difftest/ruffle_harness.patch
    cargo build --release -p isaac_harness          # -> target/release/isaac_harness

The patch adds `isaac_harness` (a headless player with null backends that reads the recomp's test scripts) and makes
Ruffle deterministic for the comparison: `ISAAC_AVMRNG=<seed>` seeds Flash's random generator, `getTimer()` follows
a virtual clock (`frame * 1000 / fps`), and `[ball]`, `[eval]`, `[call]`, `[rng]`, `[prop]` lines use the same format
as the recomp's `eval balls`, `eval <path>`, `eval call ...`, `eval rng`, `eval props <path>`. Tracing:
`ISAAC_RNGTRACE=1` (who asks for random numbers, both sides), `ISAAC_HITTRACE=1` (every `hitTest(x, y, flag)`), and
in the recomp `ISAAC_GOTOTRACE=<path part>` (gotoAndStop/gotoAndPlay on matching clips).

## Running

    export HARNESS=.../target/release/isaac_harness RECOMP=.../build/isaac
    export SWF_OG=isaac1080_OG.swf SWF_WOTL=isaac1080.swf SWF_ETERNAL=isaac1080_ETERNAL.swf   # HD-patched SWFs (NOTES.md)
    export EXE_OG=Isaac-OG.exe EXE_WOTL=Isaac.exe EXE_ETERNAL=Isaac-ETERNAL.exe              # the user's game files
    ./batch.sh og 10 11 12        # enemy types, one at a time, 600 ticks of random input in the first room
    ./batch_rooms.sh og 100 0 1 2 # 12 random rooms per floor (teleport), floors 1..3, god mode
    ./batch_kill.sh og 700 10 11  # each enemy type auto-aimed at until it dies (deaths, splits, drops), god mode
    ./batch_boss.sh wotl 1800 0 b1-0 b2-0 h1 a3-1   # boss room templates on their floor, auto-aim, until the boss dies
    ./batch_item.sh og 800 1 2 3  # items picked up from a pedestal, then fights with auto-aim, active used every 90 ticks
    ./batch_item.sh og 600 p1 p9 p30   # p<col>: pills 1-6, cards 7-28 / 70-74, trinkets 29-61 (used with Q)
    python3 cmpf.py out/X_og_ruffle.txt out/X_og_recomp.txt 5 [fields,to,ignore]   # first differences

The recomp runs with `ISAAC_NOMDM=1` (no Zinc `mdm` object, like Ruffle) and a fresh save.
