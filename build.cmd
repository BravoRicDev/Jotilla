@echo off
setlocal
cd /d "%~dp0"
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" call :compiler
if errorlevel 1 exit /b 1
if not exist build mkdir build
if not exist dist mkdir dist
cl /nologo /TC /std:c17 /W4 /WX /O2 /MT tools\make_icon.c /Fo"build\make_icon.obj" /Fe"build\make_icon.exe"
if errorlevel 1 exit /b 1
build\make_icon.exe
if errorlevel 1 exit /b 1
rc /nologo /fo build\picoslip.res src\picoslip.rc
if errorlevel 1 exit /b 1
if /i "%~1"=="test-only" goto tests
if /i "%~1"=="screenshots-only" goto screenshots
if /i "%~1"=="profile-only" goto profile
cl /nologo /TC /std:c17 /W4 /WX /O2 /MT /utf-8 src\picoslip.c /Fo"build\picoslip.obj" /Fe"dist\PicoSlip.exe" /link build\picoslip.res /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
rc /nologo /fo build\setup.res src\setup.rc
if errorlevel 1 exit /b 1
cl /nologo /TC /std:c17 /W4 /WX /O2 /MT /utf-8 src\setup.c /Fo"build\setup.obj" /Fe"dist\PicoSlip-Setup-x64.exe" /link build\setup.res /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest user32.lib shell32.lib ole32.lib uuid.lib advapi32.lib
if errorlevel 1 exit /b 1
if /i "%~1"=="test" goto tests
if /i "%~1"=="screenshots" goto screenshots
echo Ready: %CD%\dist\PicoSlip.exe
exit /b 0

:tests
cl /nologo /TC /std:c17 /W4 /WX /O2 /MT /utf-8 tests\tests.c /Fo"build\tests.obj" /Fe"build\tests.exe" /link build\picoslip.res /SUBSYSTEM:CONSOLE /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
build\tests.exe %~2
exit /b %errorlevel%

:screenshots
cl /nologo /TC /std:c17 /W4 /WX /O2 /MT /utf-8 tests\demo.c /Fo"build\demo.obj" /Fe"build\demo.exe" /link build\picoslip.res /SUBSYSTEM:CONSOLE /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
build\demo.exe
exit /b %errorlevel%

:profile
cl /nologo /TC /std:c17 /W4 /WX /O2 /MT /utf-8 tools\profile.c /Fo"build\profile.obj" /Fe"build\profile.exe" /link user32.lib psapi.lib
if errorlevel 1 exit /b 1
build\profile.exe "%CD%\dist\PicoSlip.exe"
exit /b %errorlevel%

:compiler
set "PICOSLIP_VS="
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo Install Microsoft Build Tools with C/C++ tools and the Windows SDK.
    exit /b 1
)
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PICOSLIP_VS=%%I"
if not defined PICOSLIP_VS exit /b 1
call "%PICOSLIP_VS%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
exit /b %errorlevel%
