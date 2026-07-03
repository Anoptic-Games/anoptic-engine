@echo off
setlocal enabledelayedexpansion

:: Navigate to the script's directory
cd /d %~dp0

:: Toolchain discovery. If clang/cmake are already on PATH (an MSYS2 shell, or a dev who set it
:: up), these are no-ops. Otherwise fall back to the conventional MSYS2 / CMake install dirs so a
:: bare cmd.exe can build. Override by setting the var first, e.g.  set MSYS2_CLANG=D:\... ^& build.bat 1
:: Vulkan is NOT handled here: the SDK installer exports VULKAN_SDK system-wide and CMake's
:: find_package(Vulkan) reads it, so there is nothing to hardcode.
if not defined MSYS2_CLANG set "MSYS2_CLANG=C:\msys64\clang64\bin"
if not defined CMAKE_BIN   set "CMAKE_BIN=C:\Program Files\CMake\bin"
where clang >nul 2>&1 || set "PATH=%MSYS2_CLANG%;%PATH%"
where cmake >nul 2>&1 || set "PATH=%CMAKE_BIN%;%PATH%"
where clang >nul 2>&1 || (echo ERROR: clang not found. Install MSYS2 clang64 or set MSYS2_CLANG. & exit /b 1)
where cmake >nul 2>&1 || (echo ERROR: cmake not found. Install CMake or set CMAKE_BIN. & exit /b 1)

:: Parse the build type. BUILD_LABEL names the build dir + toolchain file;
:: CMAKE_CONFIG is the real CMake config (Tests is a Debug build that runs CTest).
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
    rem Release (-O3) build with CTest enabled: optimized test + benchmark runs. Option 1 is the same
    rem -O3 full engine build without tests. Use this, NOT 3, to benchmark the logger (3 is Debug -O0).
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

:: Create build directory if not exist
if not exist build\%BUILD_LABEL% mkdir build\%BUILD_LABEL%

:: Configure the build with MinGW Makefiles generator
cmake -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN_PATH%" -DCMAKE_BUILD_TYPE=%CMAKE_CONFIG% %EXTRA_FLAGS% -S . -B ./build/%BUILD_LABEL%
if errorlevel 1 exit /b 1

:: Build the project. Shader + asset staging is owned by CMake (root CMakeLists /
:: tests/CMakeLists) so every build flow gets it, not just this script.
cmake --build ./build/%BUILD_LABEL%
if errorlevel 1 exit /b 1

:: Run the test suite
if "%RUN_TESTS%"=="1" ctest --test-dir ./build/%BUILD_LABEL% --output-on-failure
if errorlevel 1 exit /b 1

endlocal
exit /b 0
