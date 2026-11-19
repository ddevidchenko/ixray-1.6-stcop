@echo off
setlocal enabledelayedexpansion

:: Navigate to repository root
set "REPO_ROOT=%~dp0.."
pushd "%REPO_ROOT%"

set "CONFIG=Release"
set "ACTION=build"
set "TARGET="

:parse_args
if "%~1"=="" goto after_args
if /i "%~1"=="clean" (
    set "ACTION=clean"
    shift
    goto parse_args
)
if /i "%~1"=="rebuild" (
    set "ACTION=rebuild"
    shift
    goto parse_args
)
if /i "%~1"=="debug" (
    set "CONFIG=Debug"
    shift
    goto parse_args
)
if /i "%~1"=="release" (
    set "CONFIG=Release"
    shift
    goto parse_args
)
if /i "%~1"=="relwithdebinfo" (
    set "CONFIG=RelWithDebInfo"
    shift
    goto parse_args
)
if /i "%~1"=="-t" (
    set "TARGET=--target %~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--target" (
    set "TARGET=--target %~2"
    shift
    shift
    goto parse_args
)
set "CONFIG=%~1"
shift
goto parse_args

:after_args

set "BUILD_DIR=build\x64\Engine-Windows"

echo =======================================================
echo   IX-Ray 1.6 Compilation Tool
echo   Config : %CONFIG%
echo   Action : %ACTION%
echo   Build  : %BUILD_DIR%
echo =======================================================

if "%ACTION%"=="clean" goto do_clean
if "%ACTION%"=="rebuild" (
    call :do_clean
    if errorlevel 1 goto error_exit
)

:: Configure if CMakeCache does not exist
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [INFO] CMakeCache not found. Configuring CMake project...
    cmake -B "%BUILD_DIR%" -A x64 ^
        -DIXRAY_UTILS=OFF ^
        -DIXRAY_EDITORS=OFF ^
        -DIXRAY_MP=OFF ^
        -DIXRAY_COMPRESSOR_ONLY=OFF ^
        -DIXRAY_PLUGINS=OFF ^
        -DIXRAY_ASAN=OFF ^
        -DIXRAY_USE_R1=ON ^
        -DIXRAY_USE_R2=ON ^
        -DIXRAY_UNITYBUILD=ON ^
        -DIXRAY_PROFILER=ON
    if errorlevel 1 (
        echo [ERROR] CMake configuration failed!
        goto error_exit
    )
)

:: Build
echo [INFO] Building %CONFIG% configuration...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel %TARGET%
if errorlevel 1 (
    echo [ERROR] Compilation failed!
    goto error_exit
)

echo.
echo =======================================================
echo   Build succeeded!
echo   Executable: %BUILD_DIR%\bin\%CONFIG%\xrEngine.exe
echo =======================================================
popd
exit /b 0

:do_clean
echo [INFO] Cleaning build directory "%BUILD_DIR%"...
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to clean build directory.
        exit /b 1
    )
)
echo [INFO] Clean completed.
if "%ACTION%"=="clean" (
    popd
    exit /b 0
)
exit /b 0

:error_exit
popd
exit /b 1
