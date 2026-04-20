@echo off
setlocal
powershell -ExecutionPolicy Bypass -File "%~dp0server_validation.ps1" %*
exit /b %ERRORLEVEL%
