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
    echo Usage: %0 ^<build_type^> [^<toolchain_file_path^>]
    echo   where ^<build_type^> is one of:
    echo     1 = Release
    echo     2 = Debug
    echo     3 = Tests
    exit /b 1
)

:: Parse the second command line argument for optional toolchain path
set TOOLCHAIN_ARG=
if not "%2"=="" (
    echo Toolchain path specified: "%2"
    if not exist "%2" (
        echo Error: Toolchain file "%2" does not exist.
        exit /b 1
    )


    set TOOLCHAIN_ARG="-DCMAKE_TOOLCHAIN_FILE="%2""
)
echo TOOLCHAIN_ARG is set to: %TOOLCHAIN_ARG%


:: Create build directory if not exist
if not exist build\%BUILD_TYPE% mkdir build\%BUILD_TYPE%

:: Configure the build with MinGW Makefiles generator
:: set PATH="C:\Program Files\mingw-w64\bin;"%PATH%

cmake -G "MinGW Makefiles" %TOOLCHAIN_ARG% -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -S . -B ./build/%BUILD_TYPE%
::cmake -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE=./buildsystem/platforms/gcc-windows-x64.cmake -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -S . -B ./build/%BUILD_TYPE%

:: Build the project
cmake --build ./build/%BUILD_TYPE%

endlocal
exit /b 0