@echo off
REM Script to cleanup Large Pages registry entries for image_rtn_enum test
REM Requires Administrator privileges
REM Usage: cleanup_largepages_registry.bat <executable_name>

echo Cleaning up Large Pages registry entries...

REM Get the application name from command line argument - parameter is required
if "%~1"=="" (
    echo Error: Executable name parameter is required.
    echo Usage: cleanup_largepages_registry.bat ^<executable_name^>
    exit /b 1
)
set APP_NAME=%~1

REM Define registry path
set REG_PATH=HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\%APP_NAME%

REM Delete the registry key and all subkeys
echo Deleting registry key: %REG_PATH%
reg delete "%REG_PATH%" /f >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: Failed to delete registry key. Key may not exist or requires Administrator privileges.
    exit /b 1
)

echo Large Pages registry cleanup completed successfully.
exit /b 0
