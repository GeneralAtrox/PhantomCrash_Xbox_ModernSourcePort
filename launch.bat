@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Starts the PhantomCrash port (mirrors the Record of Lodoss War "Start Game.bat").
rem Extra arguments are passed through, e.g.  launch.bat --fullscreen
set "PHANTOMCRASH_ROOT=%~dp0"
set "PHANTOMCRASH_BUILD=%PHANTOMCRASH_ROOT%build\port"
set "PHANTOMCRASH_EXE=%PHANTOMCRASH_ROOT%build\port\port\Release\phantomcrash_port.exe"

rem CMake/MSBuild checks dependencies and only rebuilds out-of-date outputs.
set "PHANTOMCRASH_CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 (
    set "PHANTOMCRASH_CMAKE=%ProgramFiles%\CMake\bin\cmake.exe"
    if not exist "%ProgramFiles%\CMake\bin\cmake.exe" (
        echo CMake could not be found. Install CMake or add it to PATH.
        pause
        exit /b 1
    )
)

if not exist "%PHANTOMCRASH_BUILD%\CMakeCache.txt" (
    echo The game build has not been configured.
    echo Run scripts\build-port.ps1 from the project folder first.
    pause
    exit /b 1
)

echo Checking for game source changes...
rem Windows PowerShell bootstraps a real PowerShell 7 path for vcpkg, avoiding
rem the Microsoft Store execution alias. Only launch after a successful build.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%PHANTOMCRASH_ROOT%scripts\build-game.ps1" -CMake "%PHANTOMCRASH_CMAKE%"
if errorlevel 1 (
    echo.
    echo The game could not be built. See the error above.
    echo If the port is already running, close it and run this launcher again.
    pause
    exit /b 1
)

if not exist "%PHANTOMCRASH_EXE%" (
    echo The build did not produce the expected PhantomCrash port executable.
    pause
    exit /b 1
)

for /d %%D in ("%PHANTOMCRASH_ROOT%.tools\vcpkg\packages\*") do call :add_dependency "%%~fD\bin"

pushd "%PHANTOMCRASH_ROOT%" || exit /b 1
"%PHANTOMCRASH_EXE%" --root "%PHANTOMCRASH_ROOT:~0,-1%" %*
set "PHANTOMCRASH_EXIT=%ERRORLEVEL%"
popd
if not "%PHANTOMCRASH_EXIT%"=="0" (
    echo.
    echo The PhantomCrash port exited with error code %PHANTOMCRASH_EXIT%.
    pause
)
exit /b %PHANTOMCRASH_EXIT%

:add_dependency
if exist "%~1" set "PATH=%~1;%PATH%"
exit /b 0
