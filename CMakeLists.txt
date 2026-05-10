cmake_minimum_required(VERSION 3.26)
project(csplayer C)

# ── Language standard ────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ── Build options ────────────────────────────────────────────────────────────
option(ENABLE_ASAN "Enable AddressSanitizer and debug symbols" OFF)

# ── Dependency fetching ───────────────────────────────────────────────────────
include(FetchContent)

# SDL3: prefer a system-installed package; fall back to fetching from source.
find_package(SDL3 QUIET CONFIG)
if(NOT SDL3_FOUND)
    message(STATUS "SDL3 not found via find_package -- fetching from source")
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.10
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

# SDL3_mixer: same strategy; enable bundled decoders for portability.
find_package(SDL3_mixer QUIET CONFIG)
if(NOT SDL3_mixer_FOUND)
    message(STATUS "SDL3_mixer not found via find_package -- fetching from source")
    set(SDL3MIXER_MP3  ON  CACHE BOOL "" FORCE)
    set(SDL3MIXER_FLAC ON  CACHE BOOL "" FORCE)
    set(SDL3MIXER_OGG  ON  CACHE BOOL "" FORCE)
    set(SDL3MIXER_OPUS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3_mixer
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
        GIT_TAG        release-3.2.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(SDL3_mixer)
endif()

# FFTW double-precision (fftw3): required system dependency.
# FFTW's own CMake build has known codelet-compilation issues on several
# platforms (notably Apple Silicon), so we require it to be installed via
# the system package manager rather than building it from source.
#
# Install instructions:
#   macOS (Homebrew):  brew install fftw
#   Debian / Ubuntu:   sudo apt install libfftw3-dev
#   Arch Linux:        sudo pacman -S fftw
#   Windows (MSYS2):   pacman -S mingw-w64-ucrt-x86_64-fftw
find_library(FFTW3_LIB NAMES fftw3 fftw3-3
    HINTS
        "$ENV{MSYSTEM_PREFIX}/lib"   # MSYS2
        /opt/homebrew/lib            # Homebrew on Apple Silicon
        /usr/local/lib               # Homebrew on Intel Mac / manual installs
        /usr/lib)
find_path(FFTW3_INCLUDE_DIR NAMES fftw3.h
    HINTS
        "$ENV{MSYSTEM_PREFIX}/include"
        /opt/homebrew/include
        /usr/local/include
        /usr/include)

if(NOT FFTW3_LIB OR NOT FFTW3_INCLUDE_DIR)
    if(APPLE)
        message(FATAL_ERROR
            "FFTW3 not found.\n"
            "Install it with Homebrew and re-run cmake:\n"
            "  brew install fftw")
    elseif(UNIX)
        message(FATAL_ERROR
            "FFTW3 not found.\n"
            "Install it with your package manager and re-run cmake:\n"
            "  Debian/Ubuntu: sudo apt install libfftw3-dev\n"
            "  Arch Linux:    sudo pacman -S fftw")
    else()
        message(FATAL_ERROR
            "FFTW3 not found.\n"
            "Install it in an MSYS2 UCRT64 shell and re-run cmake:\n"
            "  pacman -S mingw-w64-ucrt-x86_64-fftw")
    endif()
endif()

set(FFTW3_TARGET  "${FFTW3_LIB}")
set(FFTW3_INCLUDE "${FFTW3_INCLUDE_DIR}")

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(csplayer
    src/main.c
    src/audio.c
    src/fft.c
    src/spectrum.c
    src/playlist.c
    src/ui.c)

target_include_directories(csplayer PRIVATE src ${FFTW3_INCLUDE})

# ── Compiler flags ────────────────────────────────────────────────────────────
if(MSVC)
    # MSVC equivalents of -Wall -Werror -Wpedantic
    target_compile_options(csplayer PRIVATE /W4 /WX /permissive-
        $<$<BOOL:${ENABLE_ASAN}>:/fsanitize=address /Zi>)
else()
    target_compile_options(csplayer PRIVATE
        -Wall -Werror -Wpedantic
        $<$<BOOL:${ENABLE_ASAN}>:-g -fsanitize=address>)
    target_link_options(csplayer PRIVATE
        $<$<BOOL:${ENABLE_ASAN}>:-fsanitize=address>)
endif()

# ── Link libraries ────────────────────────────────────────────────────────────
# The math library (m) is implicit on Windows / MSVC.
if(NOT WIN32)
    set(MATH_LIB m)
endif()

target_link_libraries(csplayer PRIVATE
    SDL3::SDL3
    SDL3_mixer::SDL3_mixer
    ${FFTW3_TARGET}
    ${MATH_LIB})

# ── Windows: copy DLLs next to the executable ─────────────────────────────────
if(WIN32)
    add_custom_command(TARGET csplayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:csplayer>
        VERBATIM)
    add_custom_command(TARGET csplayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3_mixer::SDL3_mixer>
            $<TARGET_FILE_DIR:csplayer>
        VERBATIM)
endif()
