@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Resume the saved quest in the garage, skipping the intro and title.
rem The shared launcher checks/builds the game and accepts --settings overrides.
call "%~dp0launch.bat" --start garage %*
exit /b %ERRORLEVEL%
