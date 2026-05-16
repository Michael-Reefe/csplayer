cmake_minimum_required(VERSION 3.26)
project(csplayer C)

# ── Language standard ────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ── Build options ────────────────────────────────────────────────────────────
option(ENABLE_ASAN  "Enable AddressSanitizer and debug symbols" OFF)
option(ENABLE_TESTS "Build unit and integration tests"          OFF)
enable_testing()

# ── Homebrew prefix (macOS) ───────────────────────────────────────────────────
# Homebrew on Apple Silicon installs to /opt/homebrew; on Intel Macs to
# /usr/local.  Adding both to CMAKE_PREFIX_PATH lets find_package locate
# Homebrew-installed SDL3, SDL3_mixer, and SDL3_ttf without extra hints.
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH /opt/homebrew /usr/local)
endif()

# ── Dependency fetching ───────────────────────────────────────────────────────
include(FetchContent)

# ── SDL3 ─────────────────────────────────────────────────────────────────────
# Prefer a system-installed package.  On macOS, Homebrew packages are required
# (brew install sdl3).  On Windows, fall back to FetchContent if not installed.
# OVERRIDE_FIND_PACKAGE ensures SDL3_mixer's internal find_package(SDL3) call
# uses the FetchContent-built SDL3 rather than searching the system.
find_package(SDL3 QUIET CONFIG)
if(NOT SDL3_FOUND)
    if(APPLE)
        message(FATAL_ERROR
            "SDL3 not found.\n"
            "Install it with Homebrew and re-run cmake:\n"
            "  brew install sdl3")
    elseif(WIN32)
        message(STATUS "SDL3 not found via find_package -- fetching from source")
        FetchContent_Declare(SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG        release-3.2.10
            GIT_SHALLOW    TRUE
            OVERRIDE_FIND_PACKAGE)
        FetchContent_MakeAvailable(SDL3)
    else()
        message(FATAL_ERROR
            "SDL3 not found.  csplayer targets Windows and macOS only.")
    endif()
endif()

# ── SDL3_mixer ────────────────────────────────────────────────────────────────
# Same strategy as SDL3: require a system package on macOS, fall back to
# FetchContent on Windows.
find_package(SDL3_mixer QUIET CONFIG)
if(NOT SDL3_mixer_FOUND)
    if(APPLE)
        message(FATAL_ERROR
            "SDL3_mixer not found.\n"
            "Install it with Homebrew and re-run cmake:\n"
            "  brew install sdl3_mixer")
    elseif(WIN32)
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
    else()
        message(FATAL_ERROR
            "SDL3_mixer not found.  csplayer targets Windows and macOS only.")
    endif()
endif()

# ── SDL3_ttf ─────────────────────────────────────────────────────────────────
# Used for Unicode-aware text rendering (track titles).  Same strategy as
# SDL3 / SDL3_mixer: require a system package on macOS, FetchContent on Windows.
find_package(SDL3_ttf QUIET CONFIG)
if(NOT SDL3_ttf_FOUND)
    if(APPLE)
        message(FATAL_ERROR
            "SDL3_ttf not found.\n"
            "Install it with Homebrew and re-run cmake:\n"
            "  brew install sdl3_ttf")
    elseif(WIN32)
        message(STATUS "SDL3_ttf not found via find_package -- fetching from source")
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
        FetchContent_Declare(SDL3_ttf
            GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
            GIT_TAG        release-3.2.0
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(SDL3_ttf)
    else()
        message(FATAL_ERROR
            "SDL3_ttf not found.  csplayer targets Windows and macOS only.")
    endif()
endif()

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(csplayer
    src/main.c
    src/audio.c
    src/fft.c
    src/spectrum.c
    src/playlist.c
    src/ui.c)

target_include_directories(csplayer PRIVATE src)

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

# ── Link libraries ────────────────────────────────────────────────────────────
# The math library (m) is implicit on Windows / MSVC.
if(NOT WIN32)
    set(MATH_LIB m)
endif()

target_link_libraries(csplayer PRIVATE
    SDL3::SDL3
    SDL3_mixer::SDL3_mixer
    SDL3_ttf::SDL3_ttf
    ${MATH_LIB})

# ── Unifont (GNU Unicode bitmap font for track title display) ─────────────────
# Downloaded once at configure time; the application falls back to the built-in
# SDL_RenderDebugText (ASCII only) if the file is absent at runtime.
set(UNIFONT_OTF "${CMAKE_BINARY_DIR}/unifont.otf")
if(NOT EXISTS "${UNIFONT_OTF}")
    message(STATUS "Downloading Unifont (~5 MB) for Unicode title display...")
    file(DOWNLOAD
        "https://unifoundry.com/pub/unifont/unifont-17.0.04/font-builds/unifont-17.0.04.otf"
        "${UNIFONT_OTF}"
        SHOW_PROGRESS
        STATUS UNIFONT_STATUS
        TLS_VERIFY ON)
    list(GET UNIFONT_STATUS 0 UNIFONT_ERR)
    if(UNIFONT_ERR)
        list(GET UNIFONT_STATUS 1 UNIFONT_ERR_MSG)
        message(WARNING
            "Unifont download failed: ${UNIFONT_ERR_MSG}\n"
            "Unicode track titles will fall back to the built-in ASCII font.\n"
            "To enable Unicode: download unifont-17.0.04.otf from\n"
            "  https://unifoundry.com/unifont/index.html\n"
            "and copy it as 'unifont.otf' next to the csplayer executable.")
        file(REMOVE "${UNIFONT_OTF}")
    endif()
endif()

if(EXISTS "${UNIFONT_OTF}")
    add_custom_command(TARGET csplayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UNIFONT_OTF}"
            "$<TARGET_FILE_DIR:csplayer>/unifont.otf"
        VERBATIM
        COMMENT "Copying unifont.otf next to executable")
endif()

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
    add_custom_command(TARGET csplayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
            $<TARGET_FILE_DIR:csplayer>
        VERBATIM)
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
    install(FILES
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE:SDL3_mixer::SDL3_mixer>
        $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
        DESTINATION .)
    if(EXISTS "${CMAKE_BINARY_DIR}/unifont.otf")
        install(FILES "${CMAKE_BINARY_DIR}/unifont.otf" DESTINATION .)
    endif()
elseif(APPLE)
    install(TARGETS csplayer BUNDLE DESTINATION .)
    install(CODE "
        include(BundleUtilities)
        fixup_bundle(
            \"\${CMAKE_INSTALL_PREFIX}/csplayer.app\"
            \"\"
            \"/opt/homebrew/lib;/usr/local/lib\")
    " COMPONENT Runtime)
    if(EXISTS "${CMAKE_BINARY_DIR}/unifont.otf")
        install(FILES "${CMAKE_BINARY_DIR}/unifont.otf"
                DESTINATION csplayer.app/Contents/MacOS)
    endif()
endif()

set(CPACK_PACKAGE_NAME                "csplayer")
set(CPACK_PACKAGE_VERSION             "0.1.0")
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
