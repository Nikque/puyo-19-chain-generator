@echo off
setlocal
cd /d "%~dp0"
"%~dp0random_19_chain.exe" "%~dp0config.ini"
set "generator_exit=%errorlevel%"
pause
exit /b %generator_exit%
