add_executable(csplayer_tests
    test_main.c
    test_fft.c
    test_spectrum.c
    test_playlist.c
    test_audio.c
    test_ui.c
    test_mocks.c
    ../src/fft.c
    ../src/spectrum.c
    ../src/playlist.c
    ../src/audio.c
    ../src/ui.c)

target_compile_definitions(csplayer_tests PRIVATE CSPLAYER_TESTING)
target_include_directories(csplayer_tests PRIVATE ../src .)

if(MSVC)
    target_compile_options(csplayer_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(csplayer_tests PRIVATE -Wall -Werror -Wpedantic)
endif()

if(NOT WIN32)
    target_link_libraries(csplayer_tests PRIVATE
        SDL3::SDL3 SDL3_mixer::SDL3_mixer SDL3_ttf::SDL3_ttf m)
else()
    target_link_libraries(csplayer_tests PRIVATE
        SDL3::SDL3 SDL3_mixer::SDL3_mixer SDL3_ttf::SDL3_ttf)
endif()

add_test(NAME csplayer_tests COMMAND csplayer_tests)
