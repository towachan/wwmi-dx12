@echo off
setlocal EnableExtensions

rem Build wwmi-dx12: Release x64, straight from the repository root.
rem
rem   build.bat            Release x64
rem   build.bat /p:...     anything extra goes to MSBuild
rem
rem Needs Visual Studio 2022 with "Desktop development with C++" (or its Build Tools), and the
rem submodules: git submodule update --init --recursive

set "ROOT=%~dp0"
set "SLN=%ROOT%wwmi-dx12.sln"
set "PAUSE_AT_END="
rem Double-clicked from Explorer: keep the window open so the result can be read.
echo "%CMDCMDLINE%" | find /i "%~nx0" >nul && set "PAUSE_AT_END=1"

if not exist "%SLN%" goto :no_sln
if not exist "%ROOT%reshade_addon\sdk\include\reshade.hpp" goto :no_submodules

rem Visual Studio / Build Tools first: the msbuild on PATH can be the .NET one, which cannot build C++.
set "MSBUILD="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD for /f "delims=" %%i in ('where msbuild 2^>nul') do set "MSBUILD=%%i"
if not defined MSBUILD goto :no_msbuild

echo Build:   Release x64
echo MSBuild: "%MSBUILD%"
echo.
"%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=x64 /m %*
if errorlevel 1 goto :failed

echo.
echo [ok] %ROOT%build_addon\wwmi_dx12.addon64
echo      Copy it over your install's wwmi_dx12.addon64; the wwmi_dx12 folder (shaders, dxc) stays as is.
if defined PAUSE_AT_END pause
exit /b 0

:failed
echo.
echo [error] the build failed -- the MSBuild output above says why.
if defined PAUSE_AT_END pause
exit /b 1

:no_sln
echo [error] wwmi-dx12.sln not found next to this script.
if defined PAUSE_AT_END pause
exit /b 1

:no_submodules
echo [error] the submodules are missing (reshade_addon\sdk, third_party\3Dmigoto).
echo         Run: git submodule update --init --recursive
if defined PAUSE_AT_END pause
exit /b 1

:no_msbuild
echo [error] MSBuild not found.
echo         Install Visual Studio 2022 with "Desktop development with C++", or its Build Tools,
echo         or run this from an "x64 Native Tools Command Prompt for VS 2022".
if defined PAUSE_AT_END pause
exit /b 1
