@echo off
setlocal enabledelayedexpansion

:: Navigate to the script's directory
cd /d %~dp0

:: Toolchain discovery. Override with: set MSYS2_CLANG=... / set CMAKE_BIN=...
if not defined MSYS2_CLANG set "MSYS2_CLANG=C:\msys64\clang64\bin"
if not defined CMAKE_BIN   set "CMAKE_BIN=C:\Program Files\CMake\bin"
where clang >nul 2>&1 || set "PATH=%MSYS2_CLANG%;%PATH%"
where cmake >nul 2>&1 || set "PATH=%CMAKE_BIN%;%PATH%"
where clang >nul 2>&1 || (echo ERROR: clang not found. Install MSYS2 clang64 or set MSYS2_CLANG. & exit /b 1)
where cmake >nul 2>&1 || (echo ERROR: cmake not found. Install CMake or set CMAKE_BIN. & exit /b 1)
where ninja >nul 2>&1 || (echo ERROR: ninja not found. Install it with: pacman -S mingw-w64-clang-x86_64-ninja & exit /b 1)

:: Parse the build type.
if "%1"=="1" (
    set BUILD_LABEL=Release
    set CMAKE_CONFIG=Release
    set TOOLCHAIN_FILE=release_clang-windows-x64-mingw.cmake
    set EXTRA_FLAGS=
    set RUN_TESTS=0
) else if "%1"=="2" (
    set BUILD_LABEL=Debug
    set CMAKE_CONFIG=Debug
    set TOOLCHAIN_FILE=debug_clang-windows-x64-mingw.cmake
    set EXTRA_FLAGS=
    set RUN_TESTS=0
) else if "%1"=="3" (
    set BUILD_LABEL=Tests
    set CMAKE_CONFIG=Debug
    set TOOLCHAIN_FILE=tests_clang-windows-x64-mingw.cmake
    set EXTRA_FLAGS=-DANOPTIC_TESTS=ON
    set RUN_TESTS=1
) else if "%1"=="4" (
    echo The sanitizer profiles are Linux/macOS-only: MinGW clang on Windows supports
    echo neither TSan nor a working ASan against ucrt. Use build.sh 4/5 under WSL.
    exit /b 1
) else if "%1"=="5" (
    echo The sanitizer profiles are Linux/macOS-only: MinGW clang on Windows supports
    echo neither TSan nor a working ASan against ucrt. Use build.sh 4/5 under WSL.
    exit /b 1
) else if "%1"=="6" (
    set BUILD_LABEL=Headless
    set CMAKE_CONFIG=Debug
    set TOOLCHAIN_FILE=tests_clang-windows-x64-mingw.cmake
    set EXTRA_FLAGS=-DANOPTIC_TESTS=ON -DANOPTIC_HEADLESS=ON
    set RUN_TESTS=1
) else if "%1"=="7" (
    rem Release (-O3) tests, use for benchmarks
    set BUILD_LABEL=RelTests
    set CMAKE_CONFIG=Release
    set TOOLCHAIN_FILE=release_clang-windows-x64-mingw.cmake
    set EXTRA_FLAGS=-DANOPTIC_TESTS=ON
    set RUN_TESTS=1
) else (
    echo Usage: %0 ^<build_type^>
    echo   where ^<build_type^> is one of:
    echo     1 = Release ^(-O3 full engine build^)
    echo     2 = Debug
    echo     3 = Tests ^(Debug -O0, build + run CTest^)
    echo     4 = Tests + AddressSanitizer/UBSan ^(Linux/macOS only -- use build.sh^)
    echo     5 = Tests + ThreadSanitizer ^(Linux/macOS only -- use build.sh^)
    echo     6 = Headless tests ^(core + CTest, no renderer^)
    echo     7 = Release tests ^(-O3, build + run CTest^)
    exit /b 1
)

:: Absolute path to the toolchain file
set TOOLCHAIN_PATH=%~dp0cmake\platforms\%TOOLCHAIN_FILE%
echo TOOLCHAIN_PATH is set to: %TOOLCHAIN_PATH%

:: Reset a build dir configured with an older generator.
if exist build\%BUILD_LABEL%\CMakeCache.txt (
    findstr /c:"CMAKE_GENERATOR:INTERNAL=Ninja" build\%BUILD_LABEL%\CMakeCache.txt >nul || rmdir /s /q build\%BUILD_LABEL%
)

:: Create build directory if not exist
if not exist build\%BUILD_LABEL% mkdir build\%BUILD_LABEL%

:: Configure with the Ninja generator.
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN_PATH%" -DCMAKE_BUILD_TYPE=%CMAKE_CONFIG% %EXTRA_FLAGS% -S . -B ./build/%BUILD_LABEL%
if errorlevel 1 exit /b 1

:: Scrub all object files.
cmake --build ./build/%BUILD_LABEL% --target ano_scrub
if errorlevel 1 exit /b 1

:: Build the project.
cmake --build ./build/%BUILD_LABEL% --parallel
if errorlevel 1 exit /b 1

:: Engine-only profiles must produce a binary, fail loudly if Vulkan was missing.
if "%RUN_TESTS%"=="0" if not exist build\%BUILD_LABEL%\anopticengine.exe (
    echo ERROR: no anopticengine.exe was produced.
    echo CMake found no Vulkan SDK, so the renderer ^(and the engine target^) was skipped
    echo -- see the CMake warning above. Install the Vulkan SDK ^(VULKAN_SDK must be set^),
    echo or run build.bat 6 for the headless engine + non-GPU tests.
    exit /b 1
)

:: Run the test suite
if "%RUN_TESTS%"=="1" ctest --test-dir ./build/%BUILD_LABEL% --output-on-failure
if errorlevel 1 exit /b 1

endlocal
exit /b 0
