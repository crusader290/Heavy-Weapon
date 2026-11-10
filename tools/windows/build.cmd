@echo off
rem Native Windows x64 build (MSVC + Ninja) into build-windows\.
rem
rem   tools\windows\build.cmd                      configure + build
rem   tools\windows\build.cmd "D:\Games\Heavy Weapon Deluxe"
rem                                                also compile in a default game-data dir
rem
rem Needs Visual Studio 2022 (or Build Tools) with the "Desktop development with C++"
rem workload, which includes CMake and Ninja. SDL3/SDL3_image/SDL3_mixer are downloaded
rem from the libsdl-org GitHub releases on first configure.
setlocal
set "REPO=%~dp0..\.."

set "VSINSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSINSTALLER%\vswhere.exe" (
    echo vswhere.exe not found: install Visual Studio 2022 or its Build Tools.
    exit /b 1
)
set "VSDIR="
set "PATH=%VSINSTALLER%;%PATH%"
for /f "usebackq tokens=*" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo No Visual Studio installation with the x64 C++ tools was found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

pushd "%REPO%"
if "%~1"=="" (
    cmake --preset windows-x64 || (popd & exit /b 1)
) else (
    cmake --preset windows-x64 "-DHW_DATA_DIR=%~1" || (popd & exit /b 1)
)
cmake --build --preset windows-x64 || (popd & exit /b 1)
popd
echo.
echo Built: %REPO%\build-windows\heavyweapon.exe
