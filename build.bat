@echo off
setlocal

:: Move to the script's directory
cd /d %~dp0

:: Set the build type based on the provided argument
if "%1"=="1" (
    set "build_type=Release"
) else if "%1"=="2" (
    set "build_type=Debug"
) else if "%1"=="3" (
    set "build_type=Tests"
) else (
    echo Usage: %0 ^<build_type^>
    echo   where ^<build_type^> is one of:
    echo     1 = Release
    echo     2 = Debug
    echo     3 = Tests
    exit /b 1
)
:: Create build directory if not exist
if not exist build\%build_type% mkdir build\%build_type%

:: Set paths to MinGW compilers
set "GCC_PATH=C:\Program Files\mingw-w64\bin\gcc.exe"
set "GPP_PATH=C:\Program Files\mingw-w64\bin\g++.exe"

:: Configure the build using MinGW compilers
cmake -DCMAKE_C_COMPILER="%GCC_PATH%" -DCMAKE_CXX_COMPILER="%GPP_PATH%" -DCMAKE_BUILD_TYPE=%build_type% -S . -B ./build/%build_type%

:: Build the project
cmake --build ./build/%build_type%

:: Return to the original directory
popd

endlocal
