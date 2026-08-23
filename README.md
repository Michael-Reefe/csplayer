# csplayer

A lightweight audio player program that shows you the spectrum of your audio in real time.  Ever wanted to see the frequencies that your favorite music is putting out, without the ugly bars that audio programs so typically use?  This is the program for you!  The spectrum is shown in high detail, from 20 Hz to 20 kHz, for all of your spectral sleuthing needs (I just think it looks neat).

The frequency plot can also overlay a **scrolling spectrogram** (waterfall):
the newest spectrum is drawn at the top and the image slides downward, showing
the last 5 seconds of audio with amplitude mapped to the cubehelix colormap
(fading to transparent at the faint end so silence blends into the panel)
behind the live spectrum line; the most prominent frequencies flash bright
yellow as they enter at the top, and seeking leaves a visible break in the
waterfall.  Toggle it
with the **Spectro(g)ram** button in the toolbar above the plot (or press **G**);
it works in both the Spec and Both graph modes.

All graph controls live in that toolbar: the **(S)pec / (W)ave / (B)oth /
S(t)em** mode selectors are on the right, and the **Spectro(g)ram** and
**(D)econstruct** overlay toggles on the left.  Each label parenthesises the key
that activates it — Stem takes **T** because S already selects the spectrum
view.  A toggle greys out in modes where its subplot is not on screen.

Other keys: **Space** play/pause, **←/→** previous/next, **F11** fullscreen,
**Shift+P / Shift+S** switch between player and synth mode.

### Instrument stems

**Stem** mode splits the track by *instrument* rather than by frequency, showing
a 3×2 grid of waveforms — drums, bass, other, vocals, guitar and piano — all on
the same 20 ms time axis so they can be read against each other.  Each panel has
a checkbox that removes that instrument from the audio you are hearing, so you
can solo the drums, drop the vocals, or listen to what a mix is made of.

Separation runs an offline neural model, so it is a one-time job per track: open
Stem mode, press **Separate**, and the app stays fully usable while it works.
A named progress bar appears in the header, and playback, seeking, playlist
edits and the other graph modes all keep going — the job runs to completion no
matter what else you do, including playing other songs.  Results are cached, so
returning to a track you have already separated is instant.

Expect roughly **3× the track length**: a 3-minute song takes about 10 minutes
on a 12-core machine, and the percentage moves in small steps because each step
is a multi-second chunk of audio.  That cost is inherent to the engine, which
trades speed for low memory use so it can run anywhere; only one job runs at a
time because it already saturates the CPU.

The engine is [demucs.cpp](https://github.com/sevagh/demucs.cpp) (MIT), a C++
port of Demucs v4 inference, shipped as a sidecar binary alongside the
pre-converted 6-source model weights.  Nothing needs to be installed and nothing
is sent anywhere — separation runs entirely on your machine, offline.  The
pretrained Demucs weights originate from Meta's MIT-licensed
[demucs](https://github.com/facebookresearch/demucs) project.

### Installing

To install a pre-built program, binaries are available for windows (x86_64 architecture) and macOS (apple silicon).  Check out the releases to find downloads for them!

### Building from source

csplayer is built with CMake (3.26+) and targets **Windows and macOS**.
The build system will automatically download and compile SDL3, SDL3\_mixer, and
SDL3\_ttf from source if they are not found as system packages, so the only
hard prerequisites are a C17 compiler, CMake, and a build tool (Ninja or Make).

Two extra pieces are needed for stem separation, and both are optional:

- **The model weights.** Download `ggml-model-htdemucs-6s-f16.bin` (~53 MB) from
  the [demucs.cpp weights](https://huggingface.co/datasets/Retrobear/demucs.cpp)
  dataset and save it to `assets/`.  This is a one-time developer step — the
  packaged release contains it, so users download nothing.
- **A C++ toolchain and OpenMP**, for the sidecar engine.  MinGW GCC provides
  OpenMP already; on macOS see the note below.

Building without either is fine: pass `-DENABLE_STEMS=OFF` (or just omit the
weights) and everything except Stem mode works, with the stems view reporting
what is missing.

#### Windows — MSYS2 / UCRT64 (recommended)

1. Install [MSYS2](https://www.msys2.org/) and open an **UCRT64** shell.
2. Install the toolchain:
   ```sh
   pacman -S mingw-w64-ucrt-x86_64-gcc \
             mingw-w64-ucrt-x86_64-cmake \
             mingw-w64-ucrt-x86_64-ninja
   ```
   SDL3, SDL3\_mixer, and SDL3\_ttf are fetched and built from source
   automatically (static libraries — no pacman SDL packages needed).
3. Configure and build:
   ```sh
   cmake -S . -B build -G Ninja
   cmake --build build
   ```
4. Run:
   ```sh
   ./build/csplayer.exe
   ```

#### Windows — Visual Studio 2022

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with the
   **Desktop development with C++** workload, plus [CMake](https://cmake.org/)
   and [Git](https://git-scm.com/).
2. Open a **Developer Command Prompt for VS 2022** and configure:
   ```cmd
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```
3. Run:
   ```cmd
   build\Release\csplayer.exe
   ```

Alternatively, with Clang + Ninja from the same Developer Command Prompt:
```cmd
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang-cl
cmake --build build
```

#### macOS

1. Install [Homebrew](https://brew.sh/), then:
   ```sh
   brew install cmake ninja libomp
   ```
   SDL3, SDL3\_mixer, and SDL3\_ttf are fetched and built from source automatically
   (static libraries — no Homebrew SDL packages needed).  `libomp` is only needed
   for the stem-separation engine: Apple Clang ships no OpenMP runtime, and
   demucs.cpp requires one.  The build locates it via `brew --prefix libomp`
   automatically; pass `-DOpenMP_ROOT=...` to override, or `-DENABLE_STEMS=OFF`
   to skip the engine entirely.
2. Configure and build:
   ```sh
   cmake -S . -B build -G Ninja
   cmake --build build
   ```
3. Run:
   ```sh
   ./build/csplayer
   ```

Note that for a redistributable `.app`, `libomp.dylib` must be bundled into
`Contents/Frameworks` and the engine's load path fixed up with
`install_name_tool`, or libomp linked statically — otherwise stem separation
fails on machines without Homebrew.

### Running the tests

The test suite covers every source module with 314 unit and integration
tests.  Enable it at configure time with `-DENABLE_TESTS=ON`:

```sh
cmake -S . -B build -G Ninja -DENABLE_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests run without real audio hardware or a GPU: SDL's built-in `dummy` audio
and video drivers are used, and the draw tests render through the software
renderer over the dummy driver's in-memory framebuffer (so no OpenGL/EGL
library is required).  Exit code is non-zero if any test fails, making this
suitable for CI.

### Building a packaged binary

#### Windows — ZIP

After a normal build, run CPack from inside the build directory:

```sh
cmake -S . -B build -G Ninja
cmake --build build
cd build && cpack -G ZIP
```

This produces `csplayer-<version>-win64.zip` containing just `csplayer.exe`
and `unifont.otf`.  SDL3, SDL3_mixer, and SDL3_ttf are statically linked, so
there are no DLL dependencies.  The zip can be extracted anywhere and run
without installing anything.

#### macOS — DMG

```sh
cmake -S . -B build -G Ninja
cmake --build build
cd build && cpack -G DragNDrop
```

This produces a `csplayer-<version>-Darwin.dmg` containing a self-contained
`csplayer.app` bundle.  SDL3, SDL3\_mixer, and SDL3\_ttf are statically linked,
so the app has no external dylib dependencies.

---

#### AddressSanitizer build

ASan is supported on macOS (GCC or Clang) and on Windows with MSVC
(VS 2019 16.9+). **MinGW GCC on Windows does not ship `libasan`** — CMake
will reject `-DENABLE_ASAN=ON` with a clear error if you try.

**macOS:**
```sh
cmake -S . -B build-asan -G Ninja -DENABLE_ASAN=ON
cmake --build build-asan
./build-asan/csplayer
```

**Windows — Visual Studio 2022:**
```cmd
cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON
cmake --build build-asan --config Debug
build-asan\Debug\csplayer.exe
```

