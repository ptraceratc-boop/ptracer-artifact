@echo off
REM Script to setup Large Pages registry entries for image_rtn_enum test
REM Requires Administrator privileges
REM Usage: setup_largepages_registry.bat <executable_name> <registry_dll_name> <privilege_dll_name>

echo Setting up Large Pages registry entries...

REM Get the application name from command line argument - parameter is required
if "%~1"=="" (
    echo Error: Executable name parameter is required.
    echo Usage: setup_largepages_registry.bat ^<executable_name^> ^<registry_dll_name^> ^<privilege_dll_name^>
    exit /b 1
)
set APP_NAME=%~1

REM Get the DLL names from command line arguments
if "%~2"=="" (
    echo Error: Registry DLL name parameter is required.
    echo Usage: setup_largepages_registry.bat ^<executable_name^> ^<registry_dll_name^> ^<privilege_dll_name^>
    exit /b 1
)
set REGISTRY_DLL=%~2

if "%~3"=="" (
    echo Error: Privilege DLL name parameter is required.
    echo Usage: setup_largepages_registry.bat ^<executable_name^> ^<registry_dll_name^> ^<privilege_dll_name^>
    exit /b 1
)
set PRIVILEGE_DLL=%~3

REM Define registry paths 
set REG_PATH=HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\%APP_NAME%
set DLL_PATH=%REG_PATH%\LargePageDLLs

REM Create the main registry key and set UseLargePages
echo Creating registry key: %REG_PATH%
reg add "%REG_PATH%" /v UseLargePages /t REG_DWORD /d 1 /f >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: Failed to create main registry key.
    exit /b 1
)

REM Create the LargePageDLLs subkey
echo Creating registry key: %DLL_PATH%
reg add "%DLL_PATH%" /f >nul 2>&1

REM Add DLL entries
echo Adding %REGISTRY_DLL% to LargePageDLLs...
reg add "%DLL_PATH%" /v %REGISTRY_DLL% /t REG_DWORD /d 1 /f >nul 2>&1

echo Adding %PRIVILEGE_DLL% to LargePageDLLs...
reg add "%DLL_PATH%" /v %PRIVILEGE_DLL% /t REG_DWORD /d 1 /f >nul 2>&1

echo Large Pages registry setup completed successfully.
echo   Application: %APP_NAME%
echo   Registry DLL: %REGISTRY_DLL%
echo   Privilege DLL: %PRIVILEGE_DLL%
exit /b 0
