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
        GIT_TAG        release-3.0.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(SDL3_mixer)
endif()

# FFTW double-precision (fftw3): prefer system installation via find_library,
# fall back to building from source.
# NOTE: csplayer is GPL-3.0 licensed, which is compatible with FFTW's GPL.
find_library(FFTW3_LIB NAMES fftw3 fftw3-3
    HINTS "$ENV{MSYSTEM_PREFIX}/lib" /usr/local/lib /usr/lib)
find_path(FFTW3_INCLUDE_DIR NAMES fftw3.h
    HINTS "$ENV{MSYSTEM_PREFIX}/include" /usr/local/include /usr/include)
if(FFTW3_LIB AND FFTW3_INCLUDE_DIR)
    set(FFTW3_TARGET  "${FFTW3_LIB}")
    set(FFTW3_INCLUDE "${FFTW3_INCLUDE_DIR}")
else()
    # Fetch and build FFTW from source (double precision is the default).
    message(STATUS "fftw3 not found -- building from source")
    set(ENABLE_THREADS     OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPENMP      OFF CACHE BOOL "" FORCE)
    set(ENABLE_LONG_DOUBLE OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(fftw3
        URL      https://www.fftw.org/fftw-3.3.10.tar.gz
        URL_HASH SHA256=56c932549852cddcfafdab3820b0200a7d6dd58b84d50e0db6a74a246c7a373d)
    FetchContent_MakeAvailable(fftw3)
    set(FFTW3_TARGET  fftw3)
    set(FFTW3_INCLUDE "${fftw3_SOURCE_DIR}/api")
endif()

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
