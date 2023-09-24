@echo off
setlocal enabledelayedexpansion

:: Navigate to the script's directory
cd /d %~dp0

:: Parse the command line argument
set BUILD_TYPE=Debug

if "%1"=="1" (
    set BUILD_TYPE=Release
) else if "%1"=="2" (
    set BUILD_TYPE=Debug
) else if "%1"=="3" (
    set BUILD_TYPE=Tests
) else (
    echo Usage: %0 ^<build_type^>
    echo   where ^<build_type^> is one of:
    echo     1 = Release
    echo     2 = Debug
    echo     3 = Tests
    exit /b 1
)

:: Set the toolchain path based on the build type
if "%BUILD_TYPE%"=="Release" (
    set TOOLCHAIN_PATH=cmake\platforms\release_clang-windows-x64-mingw.cmake
) else if "%BUILD_TYPE%"=="Debug" (
    set TOOLCHAIN_PATH=cmake\platforms\debug_clang-windows-x64-mingw.cmake
) else if "%BUILD_TYPE%"=="Tests" (
    set TOOLCHAIN_PATH=cmake\platforms\tests_clang-windows-x64-mingw.cmake
) else (
    echo "BORKED"
)

:: Convert relative path to absolute path
pushd "%~dp0"
set TOOLCHAIN_PATH=%CD%\%TOOLCHAIN_PATH%
popd

echo TOOLCHAIN_PATH is set to: %TOOLCHAIN_PATH%

:: Create build directory if not exist
if not exist build\%BUILD_TYPE% mkdir build\%BUILD_TYPE%

:: Configure the build with MinGW Makefiles generator
cmake -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN_PATH%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -S . -B ./build/%BUILD_TYPE%

:: Build the project
cmake --build ./build/%BUILD_TYPE%

endlocal
exit /b 0