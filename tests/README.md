# Headless test scripts

- `start_game.txt` — from the title screen into a new run (ticks 210-862). Prefix for other scripts.
- `creep_hittest.txt` — regression for the creep/blood hitTest (Eternal): expected output
  `[call] _root.a.shit -> true` then `-> false`.

      build/isaac --headless --norender --script tests/creep_hittest.txt Isaac-ETERNAL.exe | grep call
