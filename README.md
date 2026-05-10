# csplayer

A lightweight audio player program that shows you the spectrum of your audio in real time.  Ever wanted to see the frequencies that your favorite music is putting out, without the ugly bars that audio programs so typically use?  This is the program for you!  The spectrum is shown in high detail, from 20 Hz to 20 kHz, for all of your spectral sleuthing needs (I just think it looks neat).

### Building from source

csplayer is built with CMake (3.26+). The build system will automatically
download and compile SDL3, SDL3\_mixer, and FFTW3 from source if they are not
found as system packages, so the only hard prerequisites are a C17 compiler,
CMake, and a build tool (Ninja or Make).

#### Windows — MSYS2 / UCRT64 (recommended)

1. Install [MSYS2](https://www.msys2.org/) and open an **UCRT64** shell.
2. Install the toolchain and optional system libraries:
   ```sh
   pacman -S mingw-w64-ucrt-x86_64-gcc \
             mingw-w64-ucrt-x86_64-cmake \
             mingw-w64-ucrt-x86_64-ninja \
             mingw-w64-ucrt-x86_64-SDL3 \
             mingw-w64-ucrt-x86_64-SDL3_mixer \
             mingw-w64-ucrt-x86_64-fftw
   ```
   The SDL3 and FFTW packages are optional — CMake will fetch them from source
   if they are absent — but installing them saves significant build time.
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
   brew install cmake ninja sdl3 sdl3_mixer fftw
   ```
2. Configure and build:
   ```sh
   cmake -S . -B build -G Ninja
   cmake --build build
   ```
3. Run:
   ```sh
   ./build/csplayer
   ```

#### Linux — Debian / Ubuntu

1. Install dependencies:
   ```sh
   sudo apt install build-essential cmake ninja-build \
                    libsdl3-dev libfftw3-dev
   ```
   SDL3\_mixer does not yet have a widely-available apt package; CMake will
   fetch and build it automatically if it is not found.
2. Configure and build:
   ```sh
   cmake -S . -B build -G Ninja
   cmake --build build
   ```
3. Run:
   ```sh
   ./build/csplayer
   ```

#### Linux — Arch Linux

1. Install dependencies:
   ```sh
   sudo pacman -S gcc cmake ninja sdl3 fftw
   ```
   As with Debian, SDL3\_mixer will be fetched automatically if absent.
2. Configure and build:
   ```sh
   cmake -S . -B build -G Ninja
   cmake --build build
   ```

#### AddressSanitizer build

An ASan-instrumented build can be enabled on any platform that supports it
(GCC/Clang on Linux and macOS; MSVC on Windows with VS 2019 16.9+):

```sh
cmake -S . -B build-asan -G Ninja -DENABLE_ASAN=ON
cmake --build build-asan
./build-asan/csplayer
```

