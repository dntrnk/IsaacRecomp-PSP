# Testing only: the game logic built for aarch64 (Linux) and run under qemu-user,
# to check on the Switch's architecture that the results are identical to PC.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(A64_SYSROOT "$ENV{A64_SYSROOT}" CACHE PATH "root with aarch64 SDL2/libs for the tests")
set(CMAKE_FIND_ROOT_PATH ${A64_SYSROOT} /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(NO_SYSTEM_GL ON)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
