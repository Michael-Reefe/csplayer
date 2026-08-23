add_executable(csplayer_tests
    test_main.c
    test_fft.c
    test_spectrum.c
    test_playlist.c
    test_audio.c
    test_synth.c
    test_ui.c
    test_metadata.c
    test_mocks.c
    test_stems.c
    ../src/fft.c
    ../src/spectrum.c
    ../src/playlist.c
    ../src/metadata.c
    ../src/audio.c
    ../src/ui.c
    ../src/synth.c
    ../src/stems.c)

target_compile_definitions(csplayer_tests PRIVATE CSPLAYER_TESTING)
target_include_directories(csplayer_tests PRIVATE ../src . ${tinysoundfont_SOURCE_DIR})

if(MSVC)
    target_compile_options(csplayer_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(csplayer_tests PRIVATE -Wall -Werror -Wpedantic)
endif()

# Line and branch coverage for the modules under test.  Applied to this target
# only, so the SDL dependencies are not instrumented:
#   cmake -S . -B build-cov -DENABLE_TESTS=ON -DENABLE_COVERAGE=ON
#   cmake --build build-cov && ./build-cov/tests/csplayer_tests
#   gcov -b -o build-cov/tests/CMakeFiles/csplayer_tests.dir/__/src ../src/*.c
if(ENABLE_COVERAGE)
    if(MSVC)
        message(FATAL_ERROR "ENABLE_COVERAGE requires GCC or Clang, not MSVC.")
    endif()
    target_compile_options(csplayer_tests PRIVATE --coverage -O0 -g)
    target_link_options(csplayer_tests PRIVATE --coverage)
endif()

if(NOT WIN32)
    set(MATH_LIB m)
endif()
target_link_libraries(csplayer_tests PRIVATE
    SDL3::SDL3-static SDL3_mixer::SDL3_mixer-static SDL3_ttf::SDL3_ttf-static
    ${MATH_LIB})

add_test(NAME csplayer_tests COMMAND csplayer_tests)

# Give the test binary the absolute path to gm.sf2 so it works regardless of
# the working directory.  If the file is absent, the synth tests skip cleanly.
if(EXISTS "${GM_SF2}")
    target_compile_definitions(csplayer_tests PRIVATE
        SF2_PATH="${GM_SF2}")
endif()

if(EXISTS "${UNIFONT_OTF}")
    target_compile_definitions(csplayer_tests PRIVATE
        UNIFONT_PATH="${UNIFONT_OTF}")
endif()
