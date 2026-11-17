@echo off
rem Dev build helper (Windows). Override with env vars on another machine:
rem   FORGE_VCVARS    full path to vcvars64.bat
rem   FORGE_CMAKE_BIN dir containing cmake.exe (and FORGE_NINJA_BIN for ninja.exe); optional if already on PATH
rem   FORGE_BUILD_DIR build tree (default build\debug)
rem Usage: tools\dev\build.bat [extra cmake --build args]
if not defined FORGE_VCVARS set "FORGE_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined FORGE_CMAKE_BIN set "FORGE_CMAKE_BIN=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if not defined FORGE_NINJA_BIN set "FORGE_NINJA_BIN=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if not defined FORGE_BUILD_DIR set "FORGE_BUILD_DIR=build\debug"
call "%FORGE_VCVARS%" >nul 2>&1
set "PATH=%FORGE_CMAKE_BIN%;%FORGE_NINJA_BIN%;%PATH%"
cd /d "%~dp0..\.."
rem First time on a machine: cmake --preset debug   (then this script builds)
cmake --build %FORGE_BUILD_DIR% %*
