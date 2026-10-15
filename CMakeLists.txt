cmake_minimum_required(VERSION 3.16)
project(IsaacWotL C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(NINTENDO_SWITCH)
  set(SDL2_INCLUDE_DIRS ${PORTLIBS}/include/SDL2)
elseif(ANDROID)
  # Android (built by Gradle, see android/): SDL2 is compiled from its release sources as libSDL2.so,
  # which SDL's Java activity loads together with our libmain.so
  include(FetchContent)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
    URL_HASH SHA256=24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4)
  FetchContent_MakeAvailable(SDL2)
  # SDL 2.30's AAudio backend (Android): ask for the low-latency mode (SDL doesn't; Android then picks large buffers,
  # a noticeable sound delay), and make the stream's buffer hold at least two of SDL's blocks - in low-latency mode it
  # can be smaller than one block.
  # Patched after configuring and before compiling; idempotent.
  set(_aa_funcs ${sdl2_SOURCE_DIR}/src/audio/aaudio/SDL_aaudiofuncs.h)
  set(_aa_src ${sdl2_SOURCE_DIR}/src/audio/aaudio/SDL_aaudio.c)
  file(READ ${_aa_funcs} _t)
  foreach(_f AAudioStreamBuilder_setPerformanceMode AAudioStream_setBufferSizeInFrames AAudioStream_getBufferCapacityInFrames
             AAudioStream_getBufferSizeInFrames AAudioStream_getFramesPerBurst)
    string(REGEX REPLACE "SDL_PROC_UNUSED\\(([^,]+), ${_f}," "SDL_PROC(\\1, ${_f}," _t "${_t}")
  endforeach()
  file(WRITE ${_aa_funcs} "${_t}")
  file(READ ${_aa_src} _t)
  if(NOT _t MATCHES "PERFORMANCE_MODE_LOW_LATENCY")
    string(REPLACE "ctx.AAudioStreamBuilder_setErrorCallback(ctx.builder, aaudio_errorCallback, private);"
      "ctx.AAudioStreamBuilder_setErrorCallback(ctx.builder, aaudio_errorCallback, private);\n    ctx.AAudioStreamBuilder_setPerformanceMode(ctx.builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);" _t "${_t}")
    string(REPLACE "ctx.AAudioStreamBuilder_setErrorCallback(ctx.builder, aaudio_errorCallback, hidden);"
      "ctx.AAudioStreamBuilder_setErrorCallback(ctx.builder, aaudio_errorCallback, hidden);\n    ctx.AAudioStreamBuilder_setPerformanceMode(ctx.builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);" _t "${_t}")
    string(REPLACE "    this->spec.freq = ctx.AAudioStream_getSampleRate(private->stream);"
      "    { int32_t want = 2 * (int32_t)this->spec.samples, cap = ctx.AAudioStream_getBufferCapacityInFrames(private->stream); if (cap > 0 && want > cap) want = cap; ctx.AAudioStream_setBufferSizeInFrames(private->stream, want); }\n    this->spec.freq = ctx.AAudioStream_getSampleRate(private->stream);" _t "${_t}")
    string(REPLACE "        /* We handle this better in SDL3"
      "        { int32_t want = 2 * (int32_t)device->spec.samples, cap = ctx.AAudioStream_getBufferCapacityInFrames(hidden->stream); if (cap > 0 && want > cap) want = cap; ctx.AAudioStream_setBufferSizeInFrames(hidden->stream, want); }\n        /* We handle this better in SDL3" _t "${_t}")
    file(WRITE ${_aa_src} "${_t}")
  endif()
  # SDL's aaudio_PlayDevice writes each block with a 1 ms timeout and drops whatever didn't fit. With the low-latency
  # buffer usually full, most of every block was thrown away: the choppy "engine trying to start" sound (it went
  # away when screen recording re-routed the stream). Write the whole block (blocking is what paces SDL's audio
  # thread) and, like Oboe's latency tuner, grow the buffer by one burst whenever the device reports an underrun.
  # Also: when the audio output changes and SDL can't re-open the stream, its next write used a NULL stream (crash,
  # the app restarted); now it waits and retries.
  file(READ ${_aa_src} _t)
  if(NOT _t MATCHES "IsaacRecomp: full writes")
    string(REPLACE "#include \"SDL_loadso.h\"" "#include \"SDL_loadso.h\"\n#include \"SDL_timer.h\"" _t "${_t}")
    string(REPLACE "    res = ctx.AAudioStream_write(private->stream, private->mixbuf, private->mixlen / private->frame_size, timeoutNanoseconds);"
      "    /* IsaacRecomp: full writes */
    if (!private->stream) {
        /* re-opening the stream failed after the output changed (headphones, Bluetooth...): SDL would crash here on
           the NULL stream. Keep the audio thread paced and try again every second. */
        static Uint32 nextTry = 0;
        if (SDL_TICKS_PASSED(SDL_GetTicks(), nextTry)) {
            nextTry = SDL_GetTicks() + 1000;
            if (RebuildAAudioStream(this) == 0) {
                return;
            }
        }
        SDL_Delay((this->spec.samples * 1000) / this->spec.freq);
        return;
    }
    {
        const int32_t total = private->mixlen / private->frame_size;
        int32_t done = 0;
        int tries = 0;
        (void)timeoutNanoseconds;
        res = 0;
        while (done < total && tries++ < 4) {
            res = ctx.AAudioStream_write(private->stream, private->mixbuf + done * private->frame_size, total - done, (int64_t)50 * 1000 * 1000);
            if (res < 0) {
                break;
            }
            done += res;
        }
        if (res >= 0) {
            static int32_t prevXRuns = 0;
            const int32_t xruns = ctx.AAudioStream_getXRunCount(private->stream);
            if (xruns > prevXRuns) {
                const int32_t burst = ctx.AAudioStream_getFramesPerBurst(private->stream);
                const int32_t cur = ctx.AAudioStream_getBufferSizeInFrames(private->stream);
                const int32_t cap = ctx.AAudioStream_getBufferCapacityInFrames(private->stream);
                if (burst > 0 && cur > 0 && cur + burst <= cap) {
                    ctx.AAudioStream_setBufferSizeInFrames(private->stream, cur + burst);
                }
            }
            prevXRuns = xruns;
            res = done;
        }
    }" _t "${_t}")
    if(NOT _t MATCHES "IsaacRecomp: full writes")
      message(FATAL_ERROR "SDL AAudio patch (full writes) did not apply: SDL_aaudio.c changed")
    endif()
    file(WRITE ${_aa_src} "${_t}")
  endif()
else()
  find_package(SDL2 REQUIRED)
endif()

file(GLOB PLUTOVG_SRC third_party/plutovg/source/*.c)
add_library(plutovg STATIC ${PLUTOVG_SRC})
target_include_directories(plutovg PUBLIC third_party/plutovg/include)
target_compile_definitions(plutovg PUBLIC PLUTOVG_BUILD_STATIC)
set_target_properties(plutovg PROPERTIES C_STANDARD 11)

# recompiled AVM1 code: gen/ (1440x1080 stage, PC) or gen720/ (960x720, Switch and Android: lighter on the CPU)
if(NINTENDO_SWITCH OR ANDROID)
  set(ISAAC_GEN_DIR gen720 CACHE STRING "")
else()
  set(ISAAC_GEN_DIR gen CACHE STRING "")
endif()
file(GLOB_RECURSE GEN_SRC ${ISAAC_GEN_DIR}/*.cpp)
add_library(isaacgen STATIC ${GEN_SRC})
target_include_directories(isaacgen PRIVATE rt)
# the generated code is huge: -O1 keeps compile times reasonable
target_compile_options(isaacgen PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-O1 -fno-strict-aliasing -w>)

set(RT_SRC rt/core.cpp rt/display.cpp rt/builtins.cpp rt/swfload.cpp rt/raster.cpp rt/render.cpp rt/snapshot.cpp rt/patches.cpp rt/platform.cpp rt/audio.cpp rt/main.cpp rt/debug.cpp rt/touch.cpp)
if(ANDROID)
  add_library(isaac SHARED ${RT_SRC})
  set_target_properties(isaac PROPERTIES OUTPUT_NAME main)   # libmain.so: the name SDL's activity loads
else()
  add_executable(isaac ${RT_SRC})
endif()
target_include_directories(isaac PRIVATE rt third_party ${SDL2_INCLUDE_DIRS})
target_link_libraries(isaac PRIVATE isaacgen plutovg)
# on Windows it can't be named isaac.exe: Steam's Isaac.exe (with the game data) usually sits in the same folder
if(WIN32)
  set_target_properties(isaac PROPERTIES OUTPUT_NAME IsaacRecomp)
endif()
if(ANDROID)
  target_link_libraries(isaac PRIVATE SDL2::SDL2 log android m)   # (OpenGL ES is loaded at run time through SDL)
elseif(NINTENDO_SWITCH)
  target_link_libraries(isaac PRIVATE SDL2 EGL glapi drm_nouveau nx m)
  # .elf -> .nro (icon + hbmenu metadata)
  set(APP_TITLE "The Binding of Isaac")
  set(APP_AUTHOR "whylurking")
  set(APP_VERSION "1.3.0")
  add_custom_command(TARGET isaac POST_BUILD
    COMMAND ${DEVKITPRO}/tools/bin/nacptool --create "${APP_TITLE}" "${APP_AUTHOR}" "${APP_VERSION}" isaac.nacp
    COMMAND ${DEVKITPRO}/tools/bin/elf2nro $<TARGET_FILE:isaac> isaac.nro --icon=${CMAKE_SOURCE_DIR}/switch/icon.jpg --nacp=isaac.nacp
    COMMENT "Generating isaac.nro")
elseif(TARGET SDL2::SDL2)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(isaac PRIVATE SDL2::SDL2main)
  endif()
  target_link_libraries(isaac PRIVATE SDL2::SDL2)
else()
  target_link_libraries(isaac PRIVATE ${SDL2_LIBRARIES})
endif()
if(WIN32)
  target_link_libraries(isaac PRIVATE opengl32)
  set_target_properties(isaac PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(isaac PRIVATE -static-libgcc -static-libstdc++ -Wl,--stack,16777216)
elseif(NOT NINTENDO_SWITCH AND NOT ANDROID)
  if(NOT NO_SYSTEM_GL)
    target_link_libraries(isaac PRIVATE GL)
  endif()
  target_link_libraries(isaac PRIVATE m)
endif()
