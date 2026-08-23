cmake_minimum_required(VERSION 3.26)
# CXX is enabled for the stem-separation sidecar (demucs.cpp), which is a
# separate executable — csplayer.exe itself stays pure C and links nothing
# from it.  Enabling the language unconditionally keeps the generated build
# identical whether or not ENABLE_STEMS is on.
project(csplayer C CXX)

# ── Language standard ────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Build options ────────────────────────────────────────────────────────────
option(ENABLE_ASAN  "Enable AddressSanitizer and debug symbols" OFF)
option(ENABLE_TESTS "Build unit and integration tests"          OFF)
# The sidecar pulls in demucs.cpp plus its vendored Eigen and libnyquist, which
# is a large clone and needs an OpenMP runtime.  Turning it OFF still produces a
# working csplayer: the stems view reports the engine as not installed.
option(ENABLE_STEMS "Build the Demucs stem-separation sidecar"  ON)
option(ENABLE_COVERAGE "Instrument the test binary for gcov coverage" OFF)
enable_testing()

# ── Dependency fetching ───────────────────────────────────────────────────────
include(FetchContent)

# Skip git-fetch on reconfigure when content is already on disk.
# Without this, any branch-based GIT_TAG (e.g. "master") triggers a network
# fetch every cmake run, hanging on slow / offline machines.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# Build all fetched libraries as static so csplayer.exe is self-contained.
# SDL3_mixer and SDL3_ttf derive their shared/static choice from BUILD_SHARED_LIBS;
# SDL3 uses its own SDL_SHARED / SDL_STATIC variables.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED        OFF CACHE BOOL "" FORCE)
set(SDL_STATIC        ON  CACHE BOOL "" FORCE)

# ── SDL3 ─────────────────────────────────────────────────────────────────────
# Always build from source so we get static libraries on every platform.
# System packages (MSYS2, Homebrew) typically only export shared targets, so
# find_package is not used.  OVERRIDE_FIND_PACKAGE ensures SDL3_mixer's
# internal find_package(SDL3) call uses this FetchContent-built copy.
if(NOT (WIN32 OR APPLE))
    message(FATAL_ERROR "csplayer targets Windows and macOS only.")
endif()
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.2
    GIT_SHALLOW    TRUE
    OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(SDL3)

# ── SDL3_mixer ────────────────────────────────────────────────────────────────
set(SDL3MIXER_MP3    ON  CACHE BOOL "" FORCE)
set(SDL3MIXER_FLAC   ON  CACHE BOOL "" FORCE)
set(SDL3MIXER_OGG    ON  CACHE BOOL "" FORCE)
set(SDL3MIXER_OPUS   OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3_mixer
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
    GIT_TAG        release-3.2.2
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(SDL3_mixer)

# ── SDL3_ttf ─────────────────────────────────────────────────────────────────
# FreeType is required by SDL3_ttf but is not always installed on the host.
# We fetch it explicitly before SDL3_ttf so that:
#   1. FetchContent_MakeAvailable(Freetype) runs FreeType's CMakeLists.txt,
#      creating the lowercase 'freetype' target.
#   2. We alias it to 'Freetype::Freetype' (the namespaced form SDL3_ttf
#      expects from a system find_package).
#   3. OVERRIDE_FIND_PACKAGE makes SDL3_ttf's internal find_package(Freetype)
#      call see the already-populated FetchContent entry (a no-op), so it
#      finds 'Freetype::Freetype' without searching the system.
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
FetchContent_Declare(Freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-3
    GIT_SHALLOW    TRUE
    OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(Freetype)
if(TARGET freetype AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG        release-3.2.2
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(SDL3_ttf)

# ── TinySoundFont (single-header SF2 renderer, MIT license) ──────────────────
# Header-only: synth.c defines TSF_IMPLEMENTATION before including tsf.h.
FetchContent_Declare(TinySoundFont
    GIT_REPOSITORY https://github.com/schellingb/TinySoundFont.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(TinySoundFont)

# ── demucs.cpp (Demucs v4 inference, MIT license) ────────────────────────────
# Instrument-stem separation engine.  Built as its own executable and shipped
# next to csplayer, which launches it with SDL_CreateProcess — so csplayer
# links nothing from it and stays pure C with no new dependencies.  All the
# C++ / Eigen / OpenMP weight is confined to this sidecar binary.
#
# Vendored Eigen and libnyquist arrive as git submodules, which FetchContent
# initialises automatically.  This is a large clone; see ENABLE_STEMS above.
if(ENABLE_STEMS)
    if(MINGW)
        # FindOpenMP prefers libgomp.dll.a (the import stub for libgomp-1.dll)
        # even when the static libgomp.a sits beside it, and an explicit library
        # path defeats -static — so the engine would still refuse to start
        # without MSYS2's DLL.  Ask the compiler where the static archive is and
        # pin FindOpenMP to it.  Must precede FetchContent_MakeAvailable below,
        # which is where demucs.cpp runs find_package(OpenMP).
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libgomp.a
                        OUTPUT_VARIABLE _gomp_static
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        if(_gomp_static AND EXISTS "${_gomp_static}")
            set(OpenMP_gomp_LIBRARY "${_gomp_static}" CACHE FILEPATH "" FORCE)
            message(STATUS "csplayer: static OpenMP runtime ${_gomp_static}")
        else()
            message(WARNING
                "Static libgomp.a not found.  The stem-separation engine will "
                "need libgomp-1.dll beside it to run.")
        endif()
    endif()

    if(APPLE)
        # demucs.cpp's CMakeLists opens with a wholesale
        #     set(CMAKE_CXX_FLAGS "-Wall -Wextra")
        # which discards everything CMake had put there — including the
        # "-isysroot <SDK>" that Apple Clang needs to locate libc++.  The result
        # is every C++ file failing with "'cmath' file not found" while the C
        # targets build fine.  Re-inject the sysroot as a directory *property*:
        # add_compile_options survives, because the subproject only overwrites
        # the variable.  Harmless when the flag is already present — a repeated
        # -isysroot with the same value is a no-op.
        if(CMAKE_OSX_SYSROOT)
            add_compile_options("-isysroot" "${CMAKE_OSX_SYSROOT}")
            message(STATUS "csplayer: pinning sysroot for demucs.cpp: ${CMAKE_OSX_SYSROOT}")
        else()
            message(WARNING
                "CMAKE_OSX_SYSROOT is empty, so the C++ standard library may not "
                "be found when building the separation engine.  Check that the "
                "command line tools are installed and 'xcrun --show-sdk-path' "
                "resolves, or build with -DENABLE_STEMS=OFF.")
        endif()

        # Apple Clang ships no OpenMP runtime, and demucs.cpp requires one.
        # Point CMake at the Homebrew copy if the caller has not already.
        if(NOT DEFINED OpenMP_ROOT)
            execute_process(COMMAND brew --prefix libomp
                            OUTPUT_VARIABLE _brew_libomp
                            OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET)
            if(_brew_libomp AND EXISTS "${_brew_libomp}")
                set(OpenMP_ROOT "${_brew_libomp}" CACHE PATH "" FORCE)
                message(STATUS "csplayer: using libomp from ${_brew_libomp}")
            else()
                message(WARNING
                    "libomp not found.  demucs.cpp needs OpenMP, which Apple Clang\n"
                    "does not provide.  Install it and reconfigure:\n"
                    "  brew install libomp\n"
                    "  cmake -S . -B build -DOpenMP_ROOT=$(brew --prefix libomp)\n"
                    "Or build without stem separation: -DENABLE_STEMS=OFF")
            endif()
        endif()
    endif()

    # demucs.cpp declares cmake_minimum_required(VERSION 3.0), which CMake 4.x
    # refuses outright.  This is the escape hatch CMake itself names in the
    # error; scoped to this block so our own project and the SDL dependencies
    # keep their real minimums.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

    # demucs.cpp defaults USE_OPENBLAS to ON, which would add a BLAS dependency
    # we do not want to require.  Force the plain Eigen + OpenMP path.
    set(USE_OPENBLAS OFF CACHE BOOL "" FORCE)

    # The CLI passes a std::filesystem::path where a std::string is expected,
    # which GCC 15 rejects (see cmake/patch_demucscpp.cmake).  The patch is a
    # portable CMake script, so no sed or patch binary is needed on either
    # platform, and it no-ops once upstream fixes this.
    FetchContent_Declare(demucscpp
        GIT_REPOSITORY https://github.com/sevagh/demucs.cpp.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
        PATCH_COMMAND  ${CMAKE_COMMAND} -DSRC=<SOURCE_DIR>
                       -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_demucscpp.cmake")
    FetchContent_MakeAvailable(demucscpp)

    unset(CMAKE_POLICY_VERSION_MINIMUM)

    # libnyquist (vendored by demucs.cpp for audio I/O) bundles WavPack, which
    # reaches for the MSVC intrinsics _BitScanForward / _BitScanReverse on any
    # _WIN32 / _WIN64 target without checking which compiler is in use.  MinGW
    # GCC defines those macros and does provide the intrinsics, but only via
    # <intrin.h>, which WavPack never includes — so three call sites fail to
    # compile.  Force-including the header supplies all of them at once and
    # leaves WavPack's own platform choices untouched.
    if(TARGET libnyquist AND WIN32 AND NOT MSVC)
        target_compile_options(libnyquist PRIVATE -include intrin.h)
    endif()

    # The engine uses OpenMP and C++, which MinGW links dynamically by default:
    # the bare executable needs libgomp-1.dll, libstdc++-6.dll and
    # libgcc_s_seh-1.dll beside it and will not even start without them.  A
    # packaged build must not depend on the developer's MSYS2 install, so link
    # the runtimes in.  (macOS cannot link fully statically; see the libomp
    # bundling note in README.md for the equivalent step there.)
    if(TARGET demucs_mt.cpp.main AND MINGW)
        target_link_options(demucs_mt.cpp.main PRIVATE -static)
    endif()
endif()

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(csplayer
    src/main.c
    src/audio.c
    src/fft.c
    src/spectrum.c
    src/playlist.c
    src/metadata.c
    src/ui.c
    src/synth.c
    src/stems.c)

target_include_directories(csplayer PRIVATE src ${tinysoundfont_SOURCE_DIR})

# ── Compiler flags ────────────────────────────────────────────────────────────
# MinGW GCC on Windows does not ship libasan, so -fsanitize=address will fail
# at link time.  Reject the combination early with a clear message.
if(ENABLE_ASAN AND WIN32 AND NOT MSVC)
    message(FATAL_ERROR
        "AddressSanitizer is not supported by MinGW GCC on Windows.\n"
        "Use Visual Studio 2022 (MSVC) for ASan on Windows:\n"
        "  cmake -S . -B build-asan -G \"Visual Studio 17 2022\" -A x64 -DENABLE_ASAN=ON\n"
        "  cmake --build build-asan --config Debug\n"
        "Or build on macOS where GCC/Clang ship libasan.")
endif()

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

# Static libgcc/libstdc++ on MinGW.  Without this the executable imports
# libgcc_s_seh-1.dll, so a packaged build only runs on machines that happen to
# have MSYS2 installed — which defeats the point of statically linking SDL just
# above.  ASan needs its runtime shared, so the two are mutually exclusive.
if(MINGW AND NOT ENABLE_ASAN)
    target_link_options(csplayer PRIVATE -static)
endif()

# ── Link libraries ────────────────────────────────────────────────────────────
# The math library (m) is implicit on Windows / MSVC.
if(NOT WIN32)
    set(MATH_LIB m)
endif()

target_link_libraries(csplayer PRIVATE
    SDL3::SDL3-static
    SDL3_mixer::SDL3_mixer-static
    SDL3_ttf::SDL3_ttf-static
    ${MATH_LIB})

# ── Unifont (GNU Unicode bitmap font for track title display) ─────────────────
# Bundled in assets/unifont.otf; falls back to SDL_RenderDebugText if absent.
set(UNIFONT_OTF "${CMAKE_SOURCE_DIR}/assets/unifont.otf")
if(NOT EXISTS "${UNIFONT_OTF}")
    message(WARNING
        "assets/unifont.otf not found.\n"
        "Unicode track titles will fall back to the built-in ASCII font.\n"
        "Download unifont-*.otf from https://unifoundry.com/unifont/ and save it as assets/unifont.otf")
endif()

if(EXISTS "${UNIFONT_OTF}")
    if(APPLE)
        # On macOS, SDL_GetBasePath() returns Contents/Resources/ for a .app bundle.
        # Copy there so init_font can find it at runtime.
        add_custom_command(TARGET csplayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:csplayer>/../Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UNIFONT_OTF}"
                "$<TARGET_FILE_DIR:csplayer>/../Resources/unifont.otf"
            VERBATIM
            COMMENT "Copying unifont.otf to app bundle Resources")
    else()
        add_custom_command(TARGET csplayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UNIFONT_OTF}"
                "$<TARGET_FILE_DIR:csplayer>/unifont.otf"
            VERBATIM
            COMMENT "Copying unifont.otf next to executable")
    endif()
endif()


# ── GM SoundFont (needed by synth mode) ──────────────────────────────────────
# Bundled in assets/gm.sf2; synth mode produces silence if absent.
set(GM_SF2 "${CMAKE_SOURCE_DIR}/assets/gm.sf2")
if(NOT EXISTS "${GM_SF2}")
    message(WARNING
        "assets/gm.sf2 not found.\n"
        "Synth mode will produce silence until a SoundFont is available.\n"
        "Copy any GM-compatible .sf2 file to assets/gm.sf2 to enable it.")
endif()

if(EXISTS "${GM_SF2}")
    if(APPLE)
        add_custom_command(TARGET csplayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GM_SF2}"
                "$<TARGET_FILE_DIR:csplayer>/../Resources/gm.sf2"
            VERBATIM
            COMMENT "Copying gm.sf2 to app bundle Resources")
    else()
        add_custom_command(TARGET csplayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GM_SF2}"
                "$<TARGET_FILE_DIR:csplayer>/gm.sf2"
            VERBATIM
            COMMENT "Copying gm.sf2 next to executable")
    endif()
endif()

# ── Stem separation assets (needed by the stems graph mode) ──────────────────
# Two pieces travel with the app: the sidecar engine built above, and the
# converted Demucs weights.  Both land beside the executable (or in the bundle's
# Resources on macOS), which is where stems.c looks via SDL_GetBasePath.
# Missing either one is not fatal: the stems view reports it and everything else
# keeps working.
set(STEM_MODEL "${CMAKE_SOURCE_DIR}/assets/ggml-model-htdemucs-6s-f16.bin")
if(ENABLE_STEMS AND NOT EXISTS "${STEM_MODEL}")
    message(WARNING
        "assets/ggml-model-htdemucs-6s-f16.bin not found (~53 MB).\n"
        "Stem separation will report the model as missing until it is present.\n"
        "Download the pre-converted 6-source weights from\n"
        "  https://huggingface.co/datasets/Retrobear/demucs.cpp\n"
        "and save them to assets/ggml-model-htdemucs-6s-f16.bin")
endif()

if(APPLE)
    set(CSP_ASSET_DIR "$<TARGET_FILE_DIR:csplayer>/../Resources")
else()
    set(CSP_ASSET_DIR "$<TARGET_FILE_DIR:csplayer>")
endif()

if(ENABLE_STEMS)
    # A generator expression in a POST_BUILD COMMAND does not create a build
    # dependency, so without this the copy below can run before the engine has
    # been linked on a fresh parallel build.
    add_dependencies(csplayer demucs_mt.cpp.main)
    add_custom_command(TARGET csplayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CSP_ASSET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:demucs_mt.cpp.main>" "${CSP_ASSET_DIR}"
        VERBATIM
        COMMENT "Copying stem-separation engine next to csplayer")
    if(EXISTS "${STEM_MODEL}")
        add_custom_command(TARGET csplayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${STEM_MODEL}" "${CSP_ASSET_DIR}"
            VERBATIM
            COMMENT "Copying stem-separation model next to csplayer")
    endif()
endif()

# ── macOS: .app bundle properties ─────────────────────────────────────────────
if(APPLE)
    set_target_properties(csplayer PROPERTIES
        MACOSX_BUNDLE                      TRUE
        MACOSX_BUNDLE_BUNDLE_NAME          "csplayer"
        MACOSX_BUNDLE_GUI_IDENTIFIER       "com.csplayer.csplayer")
endif()

# ── Installation + packaging (CPack) ─────────────────────────────────────────
if(WIN32)
    install(TARGETS csplayer DESTINATION .)
    if(EXISTS "${UNIFONT_OTF}")
        install(FILES "${UNIFONT_OTF}" DESTINATION .)
    endif()
    if(EXISTS "${GM_SF2}")
        install(FILES "${GM_SF2}" DESTINATION .)
    endif()
    if(ENABLE_STEMS)
        install(PROGRAMS "$<TARGET_FILE:demucs_mt.cpp.main>" DESTINATION .)
        if(EXISTS "${STEM_MODEL}")
            install(FILES "${STEM_MODEL}" DESTINATION .)
        endif()
    endif()
elseif(APPLE)
    install(TARGETS csplayer BUNDLE DESTINATION .)
    if(EXISTS "${UNIFONT_OTF}")
        install(FILES "${UNIFONT_OTF}"
                DESTINATION csplayer.app/Contents/Resources)
    endif()
    if(EXISTS "${GM_SF2}")
        install(FILES "${GM_SF2}"
                DESTINATION csplayer.app/Contents/Resources)
    endif()
    if(ENABLE_STEMS)
        install(PROGRAMS "$<TARGET_FILE:demucs_mt.cpp.main>"
                DESTINATION csplayer.app/Contents/Resources)
        if(EXISTS "${STEM_MODEL}")
            install(FILES "${STEM_MODEL}"
                    DESTINATION csplayer.app/Contents/Resources)
        endif()
    endif()
endif()

set(CPACK_PACKAGE_NAME                "csplayer")
set(CPACK_PACKAGE_VERSION             "0.5.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lightweight audio player with real-time spectrum analyser")
set(CPACK_STRIP_FILES                 TRUE)
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
endif()
include(CPack)

# ── Tests ─────────────────────────────────────────────────────────────────────
if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
