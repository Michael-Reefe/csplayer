# csplayer

A lightweight audio player program that shows you the spectrum of your audio in real time.  Ever wanted to see the frequencies that your favorite music is putting out, without the ugly bars that audio programs so typically use?  This is the program for you!  The spectrum is shown in high detail, from 20 Hz to 20 kHz, for all of your spectral sleuthing needs (I just think it looks neat).

### Installing

To install a pre-built program, binaries are available for windows (x86_64 architecture) and macOS (apple silicon).  Check out the releases to find downloads for them!

### Building from source

csplayer is built with CMake (3.26+) and targets **Windows and macOS**.
The build system will automatically download and compile SDL3, SDL3\_mixer, and
SDL3\_ttf from source if they are not found as system packages, so the only
hard prerequisites are a C17 compiler, CMake, and a build tool (Ninja or Make).

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
   brew install cmake ninja
   ```
   SDL3, SDL3\_mixer, and SDL3\_ttf are fetched and built from source automatically
   (static libraries — no Homebrew SDL packages needed).
2. Configure and build:
   ```sh
   cmake -S . -B build -G Ninja
   cmake --build build
   ```
3. Run:
   ```sh
   ./build/csplayer
   ```

### Running the tests

The test suite covers all six source modules with ~150 unit and integration
tests.  Enable it at configure time with `-DENABLE_TESTS=ON`:

```sh
cmake -S . -B build -G Ninja -DENABLE_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests run without real audio hardware: the audio integration tests use SDL's
built-in `dummy` driver and the draw tests use an offscreen renderer.  Exit
code is non-zero if any test fails, making this suitable for CI.

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

