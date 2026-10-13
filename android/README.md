# Android build

The Android version is the same native program as on PC and Switch (`rt/` + the recompiled game in `gen720/`),
built as `libmain.so` and started by SDL's Java activity. It renders with OpenGL ES 3.0 and needs a 64-bit
ARM device (arm64-v8a) with Android 7.0 or newer.

## Getting the APK without installing anything (GitHub Actions)

`.github/workflows/android.yml` builds the APK on GitHub's servers every time `rt/`, `gen720/`, `android/` or
`CMakeLists.txt` change (or by hand: **Actions → Android APK → Run workflow**). When the run finishes, download
the **IsaacRecomp-Android** artifact at the bottom of the run page: it is a .zip with `IsaacRecomp.apk` inside.
The first build takes 20-30 minutes (the recompiled game is large).

### Signing

Without extra setup the APK is signed with a throwaway debug key that changes on every build, so Android refuses
to install a new build over an old one: uninstall first (saves and settings are kept, they live in
`/IsaacRecomp`). To sign every build with the same key, create one once:

    keytool -genkeypair -v -keystore isaac.keystore -alias isaac -keyalg RSA -keysize 2048 -validity 10000
    base64 -w0 isaac.keystore     # (on Windows: certutil -encode isaac.keystore out.txt)

and add these repository secrets (**Settings → Secrets and variables → Actions**): `ANDROID_KEYSTORE_BASE64`
(the base64 text), `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS` (`isaac`) and `ANDROID_KEY_PASSWORD`.
Keep the keystore private and backed up.

## Building locally

Open the `android/` folder in Android Studio (it installs the SDK, NDK r27c and CMake it asks for), or from a
terminal with the Android SDK installed (`ANDROID_HOME` set):

    cd android
    ./gradlew assembleRelease      # -> app/build/outputs/apk/release/app-release.apk

CMake downloads the SDL2 2.30.9 sources from GitHub during the first build.

## Layout

- `app/src/main/java/org/libsdl/app/` — SDL 2.30.9's Java glue, unmodified (zlib license, `LICENSE-SDL.txt`).
- `app/src/main/java/io/github/isaacrecomp/IsaacActivity.java` — our activity: data folder, storage permission
  ("All files access" on Android 11+), launch arguments, restart after "Delete save" (`RestartActivity`).
- `app/src/main/res/mipmap-*/ic_launcher.png` — the Switch icon (`switch/icon.jpg`) at each density.
- Native side: `#ifdef __ANDROID__` in `rt/main.cpp` and `rt/platform.cpp`; touch controls in `rt/touch.cpp`;
  OpenGL ES path in `rt/render.cpp` (`g_glES`).

## Testing the Android-specific code on a PC

- `ISAAC_GLES=1` makes the PC build render through an OpenGL ES 3 context (same code path as Android).
- `ISAAC_TOUCH=1` enables the touch controls; `ISAAC_TOUCH_INJECT=file` replays synthetic touches in the real game
  loop (`<tick> down|move|up <finger> <x 0..1> <y 0..1>`, `<tick> back`, `<tick> eval <expr>`,
  `<tick> shot <file.bmp>`, `<tick> quit`). `ISAAC_INPUT_LOG=1` logs what reaches the game.
- Building with `-DISAAC_GEN_DIR=gen720` gives the same 960x720 stage as Android.
