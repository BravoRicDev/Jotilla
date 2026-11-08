@echo off
setlocal
cd /d "%~dp0"
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" call :compiler
if errorlevel 1 exit /b 1
if not exist build mkdir build
if not exist dist mkdir dist
set "JOTILA_CFLAGS=/nologo /TC /std:c17 /W4 /WX /O2 /MT /utf-8 /Fd"build\compiler.pdb""
if /i "%~1"=="test-checked" set "JOTILA_CFLAGS=/nologo /TC /std:c17 /W4 /WX /Od /MTd /RTC1 /Zi /utf-8 /Fd"build\compiler.pdb""
set "JOTILA_LIBS=build\archive.obj build\calendar.obj build\date.obj build\metadata.obj build\tags.obj build\search.obj build\location.obj build\colors.obj build\palette_picker.obj build\palette_art.obj user32.lib gdi32.lib shell32.lib dwmapi.lib comctl32.lib uxtheme.lib normaliz.lib ole32.lib uuid.lib"
set "JOTILA_MANIFEST=/MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest"
if /i "%~1"=="analyze" goto analyze
cl %JOTILA_CFLAGS% tools\make_icon.c /Fo"build\make_icon.obj" /Fe"build\make_icon.exe" /link ole32.lib windowscodecs.lib uuid.lib
if errorlevel 1 exit /b 1
build\make_icon.exe
if errorlevel 1 exit /b 1
rc /nologo /fo build\jotila.res src\jotila.rc
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% /c src\archive.c /Fo"build\archive.obj"
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% /c src\calendar.c /Fo"build\calendar.obj"
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% /c src\date.c /Fo"build\date.obj"
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% /c src\metadata.c /Fo"build\metadata.obj"
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% /c src\tags.c /Fo"build\tags.obj"
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% /c src\search.c /Fo"build\search.obj"
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% /c src\location.c /Fo"build\location.obj"
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% /c src\colors.c /Fo"build\colors.obj"
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% /c src\palette_picker.c /Fo"build\palette_picker.obj"
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% /c src\palette_art.c /Fo"build\palette_art.obj"
if errorlevel 1 exit /b 1
if /i "%~1"=="test-only" goto tests
if /i "%~1"=="screenshots-only" goto screenshots
if /i "%~1"=="profile-only" goto profile
if /i "%~1"=="search-profile" goto searchprofile
if /i "%~1"=="storage-profile" goto storageprofile
if /i "%~1"=="test-checked" goto tests
cl %JOTILA_CFLAGS% src\jotila.c /Fo"build\jotila.obj" /Fe"dist\Jotila.exe" /link build\jotila.res /SUBSYSTEM:WINDOWS %JOTILA_MANIFEST% %JOTILA_LIBS%
if errorlevel 1 exit /b 1
rc /nologo /fo build\setup.res src\setup.rc
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% src\setup.c /Fo"build\setup.obj" /Fe"dist\Jotila-Setup-x64.exe" /link build\setup.res /SUBSYSTEM:WINDOWS %JOTILA_MANIFEST% user32.lib shell32.lib ole32.lib uuid.lib advapi32.lib
if errorlevel 1 exit /b 1
if /i "%~1"=="test" goto tests
if /i "%~1"=="screenshots" goto screenshots
echo Ready: %CD%\dist\Jotila.exe
exit /b 0

:tests
cl %JOTILA_CFLAGS% tests\tests.c /Fo"build\tests.obj" /Fe"build\tests.exe" /link build\jotila.res /SUBSYSTEM:CONSOLE %JOTILA_MANIFEST% %JOTILA_LIBS%
if errorlevel 1 exit /b 1
build\tests.exe %~2
if errorlevel 1 exit /b 1
cl %JOTILA_CFLAGS% tests\setup_checks.c /Fo"build\setup_checks.obj" /Fe"build\setup_checks.exe" /link /SUBSYSTEM:CONSOLE %JOTILA_MANIFEST% user32.lib shell32.lib ole32.lib uuid.lib advapi32.lib
if errorlevel 1 exit /b 1
build\setup_checks.exe
exit /b %errorlevel%

:screenshots
cl %JOTILA_CFLAGS% tests\demo.c /Fo"build\demo.obj" /Fe"build\demo.exe" /link build\jotila.res /SUBSYSTEM:CONSOLE %JOTILA_MANIFEST% %JOTILA_LIBS%
if errorlevel 1 exit /b 1
build\demo.exe %~2
exit /b %errorlevel%

:profile
cl %JOTILA_CFLAGS% tools\profile.c /Fo"build\profile.obj" /Fe"build\profile.exe" /link user32.lib psapi.lib
if errorlevel 1 exit /b 1
build\profile.exe "%CD%\dist\Jotila.exe"
exit /b %errorlevel%

:searchprofile
cl %JOTILA_CFLAGS% tools\search_profile.c /Fo"build\search_profile.obj" /Fe"build\search_profile.exe" /link build\search.obj build\tags.obj build\metadata.obj user32.lib normaliz.lib psapi.lib
if errorlevel 1 exit /b 1
build\search_profile.exe
exit /b %errorlevel%

:storageprofile
cl %JOTILA_CFLAGS% tools\storage_profile.c /Fo"build\storage_profile.obj" /Fe"build\storage_profile.exe" /link build\jotila.res /SUBSYSTEM:CONSOLE %JOTILA_MANIFEST% %JOTILA_LIBS%
if errorlevel 1 exit /b 1
build\storage_profile.exe
exit /b %errorlevel%

:analyze
rem C6553 comes from the SDK's LCMapStringEx annotation, not application code.
rem The single search worker uses a bounded 26 KiB streaming buffer on its 1 MiB stack.
cl %JOTILA_CFLAGS% /analyze /analyze:stacksize 32768 /wd6553 /c src\jotila.c src\archive.c src\calendar.c src\date.c src\metadata.c src\tags.c src\search.c src\location.c src\colors.c src\palette_picker.c src\palette_art.c src\setup.c /Fo"build\\"
exit /b %errorlevel%

:compiler
set "JOTILA_VS="
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo Install Microsoft Build Tools with C/C++ tools and the Windows SDK.
    exit /b 1
)
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "JOTILA_VS=%%I"
if not defined JOTILA_VS exit /b 1
call "%JOTILA_VS%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
exit /b %errorlevel%
