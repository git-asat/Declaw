@echo off
setlocal

echo.
echo ================================================
echo     Declaw Build Script (Windows x64)
echo ================================================
echo.
echo Prerequisites:
echo   - .NET 8 SDK (https://dotnet.microsoft.com/download/dotnet/8.0)
echo   - Visual Studio 2022 + WDK (for driver only)
echo.

:: Check dotnet SDK is available
dotnet --list-sdks >nul 2>&1
if errorlevel 1 (
    echo [ERROR] .NET SDK not found. Please install .NET 8 SDK.
    echo         https://dotnet.microsoft.com/download/dotnet/8.0
    exit /b 1
)

set OUTPUT_DIR=%~dp0publish

:: -----------------------------------------------
:: Build Service (self-contained single-file .exe)
:: -----------------------------------------------
echo [1/3] Building DeclawService...
dotnet publish src\service\Service.csproj ^
    -c Release ^
    -r win-x64 ^
    --self-contained true ^
    -p:PublishSingleFile=true ^
    -p:IncludeNativeLibrariesForSelfExtract=true ^
    -p:EnableCompressionInSingleFile=true ^
    -p:PublishTrimmed=true ^
    -p:TrimMode=partial ^
    -o "%OUTPUT_DIR%"

if errorlevel 1 (
    echo [FAILED] Service build failed.
    exit /b 1
)
echo [OK] DeclawService.exe built.

:: -----------------------------------------------
:: Build UI / Management Console (self-contained single-file .exe)
:: -----------------------------------------------
echo.
echo [2/3] Building DeclawControl...
dotnet publish src\ui\UI.csproj ^
    -c Release ^
    -r win-x64 ^
    --self-contained true ^
    -p:PublishSingleFile=true ^
    -p:IncludeNativeLibrariesForSelfExtract=true ^
    -p:EnableCompressionInSingleFile=true ^
    -p:PublishTrimmed=true ^
    -p:TrimMode=partial ^
    -o "%OUTPUT_DIR%"

if errorlevel 1 (
    echo [FAILED] UI build failed.
    exit /b 1
)
echo [OK] DeclawControl.exe built.

:: -----------------------------------------------
:: Copy driver files and config
:: -----------------------------------------------
echo.
echo [3/3] Copying driver source and config files...
if not exist "%OUTPUT_DIR%\driver" mkdir "%OUTPUT_DIR%\driver"
copy /y src\driver\Declaw.h      "%OUTPUT_DIR%\driver\" >nul
copy /y src\driver\Declaw.c      "%OUTPUT_DIR%\driver\" >nul
copy /y src\driver\DeclawComm.c  "%OUTPUT_DIR%\driver\" >nul
copy /y src\driver\Declaw.inf    "%OUTPUT_DIR%\driver\" >nul
copy /y src\driver\Declaw.vcxproj "%OUTPUT_DIR%\driver\" >nul
copy /y src\driver\README.md       "%OUTPUT_DIR%\driver\" >nul
copy /y src\service\appsettings.json "%OUTPUT_DIR%\" >nul
copy /y README.md                  "%OUTPUT_DIR%\" >nul
copy /y LICENSE                    "%OUTPUT_DIR%\" >nul


echo.
echo ================================================
echo   BUILD COMPLETE!
echo ================================================
echo.
echo Output directory: %OUTPUT_DIR%
echo.
echo Files produced:
echo   DeclawService.exe  - Background Service (self-contained, no dependencies)
echo   DeclawControl.exe  - Management Console  (self-contained, no dependencies)
echo   appsettings.json     - Configuration (edit protected folders here)
echo   driver\              - Kernel driver source (compile with WDK)
echo.
echo NOTE: The kernel driver (.sys) must be compiled separately using
echo       Visual Studio 2022 with the Windows Driver Kit (WDK).
echo       See driver\README.md for instructions.
echo.

endlocal
