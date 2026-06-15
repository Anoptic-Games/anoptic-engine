@echo off
setlocal enabledelayedexpansion

:: Navigate to the script's directory
cd /d %~dp0

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
    set BUILD_LABEL=Tests-ASan
    set CMAKE_CONFIG=Debug
    set TOOLCHAIN_FILE=tests_clang-windows-x64-mingw.cmake
    set EXTRA_FLAGS=-DANOPTIC_TESTS=ON -DANOPTIC_SANITIZE=asan
    set RUN_TESTS=1
) else if "%1"=="5" (
    set BUILD_LABEL=Tests-TSan
    set CMAKE_CONFIG=Debug
    set TOOLCHAIN_FILE=tests_clang-windows-x64-mingw.cmake
    set EXTRA_FLAGS=-DANOPTIC_TESTS=ON -DANOPTIC_SANITIZE=tsan
    set RUN_TESTS=1
) else if "%1"=="6" (
    set BUILD_LABEL=Headless
    set CMAKE_CONFIG=Debug
    set TOOLCHAIN_FILE=tests_clang-windows-x64-mingw.cmake
    set EXTRA_FLAGS=-DANOPTIC_TESTS=ON -DANOPTIC_HEADLESS=ON
    set RUN_TESTS=1
) else (
    echo Usage: %0 ^<build_type^>
    echo   where ^<build_type^> is one of:
    echo     1 = Release
    echo     2 = Debug
    echo     3 = Tests ^(build + run CTest^)
    echo     4 = Tests + AddressSanitizer/UBSan
    echo     5 = Tests + ThreadSanitizer
    echo     6 = Headless tests ^(core + CTest, no renderer^)
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

:: Build the project
cmake --build ./build/%BUILD_LABEL%
if errorlevel 1 exit /b 1

:: Run the test suite
if "%RUN_TESTS%"=="1" ctest --test-dir ./build/%BUILD_LABEL% --output-on-failure
if errorlevel 1 exit /b 1

endlocal
exit /b 0
