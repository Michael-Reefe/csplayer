# patch_demucscpp.cmake — applied as demucs.cpp's FetchContent PATCH_COMMAND.
#
# demucs.cpp's CLI passes a std::filesystem::path where write_audio_file expects
# a std::string.  There is no implicit conversion between those types, so the
# call is ill-formed; older libstdc++ releases let it through, GCC 15 rejects it
# and the sidecar fails to build.  Adding the explicit .string() is the whole
# fix, and it is what upstream would need to do too.
#
# Written as a CMake script rather than a sed/patch invocation so it works
# identically on Windows and macOS with no extra tools on PATH.  Idempotent: a
# second run, or an upstream that has fixed this, is a no-op.
#
# Invoked with -DSRC=<the fetched source directory>.

if(NOT DEFINED SRC)
    message(FATAL_ERROR "patch_demucscpp: SRC not set")
endif()

# Every CLI app shares the same write_audio_file call.  csplayer only ships
# demucs.cpp.main, but a plain `cmake --build` builds all of them, so all six
# have to compile for the build to succeed.
set(_cli_apps
    demucs.cpp
    demucs_ft.cpp
    demucs_mt.cpp
    demucs_ft_mt.cpp
    demucs_v3.cpp
    demucs_v3_mt.cpp)

set(_changed 0)
foreach(_app IN LISTS _cli_apps)
    set(_file "${SRC}/cli-apps/${_app}")
    if(NOT EXISTS "${_file}")
        continue()
    endif()
    file(READ "${_file}" _content)
    string(REPLACE
        "write_audio_file(target_waveform, p_target)"
        "write_audio_file(target_waveform, p_target.string())"
        _patched "${_content}")
    if(NOT _patched STREQUAL _content)
        file(WRITE "${_file}" "${_patched}")
        math(EXPR _changed "${_changed} + 1")
    endif()
endforeach()

if(_changed GREATER 0)
    message(STATUS
        "patch_demucscpp: applied std::filesystem::path fix to ${_changed} file(s)")
else()
    message(STATUS
        "patch_demucscpp: nothing to do (already applied or fixed upstream)")
endif()
