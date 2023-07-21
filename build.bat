@echo off
setlocal enabledelayedexpansion

:: Move to the script's directory
pushd %~dp0

:: Parse the command line argument
if "%1"=="1" (
    set build_type=Release
) else if "%1"=="2" (
    set build_type=Debug
) else if "%1"=="3" (
    set build_type=Tests
) else (
    echo Usage: %0 ^<build_type^>
    echo   where ^<build_type^> is one of:
    echo     1 = Release
    echo     2 = Debug
    echo     3 = Tests
    exit /b 1
)

:: Create build directory if not exist
if not exist .\build\%build_type% mkdir .\build\%build_type%

:: Configure the build
cmake -DCMAKE_BUILD_TYPE=%build_type% -S . -B .\build\%build_type%

:: Build the project
cmake --build .\build\%build_type%

:: Return to the original directory
popd

endlocal
