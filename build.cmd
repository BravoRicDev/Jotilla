@echo off
setlocal
cd /d "%~dp0"
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" call :compiler
if not "%errorlevel%"=="0" exit /b 1
if not exist build mkdir build
if not exist dist mkdir dist
set "JOTILLA_CFLAGS=/nologo /TC /std:c17 /W4 /WX /O2 /MT /utf-8 /Fd"build\compiler.pdb""
if /i "%~1"=="test-checked" set "JOTILLA_CFLAGS=/nologo /TC /std:c17 /W4 /WX /Od /MTd /RTC1 /Zi /utf-8 /Fd"build\compiler.pdb""
set "JOTILLA_LIBS=build\archive.obj build\calendar.obj build\date.obj build\metadata.obj build\tags.obj build\search.obj build\location.obj build\colors.obj build\palette_picker.obj build\palette_art.obj user32.lib gdi32.lib shell32.lib dwmapi.lib comctl32.lib uxtheme.lib normaliz.lib ole32.lib uuid.lib"
set "JOTILLA_MANIFEST=/MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest"
if /i "%~1"=="analyze" goto analyze
cl %JOTILLA_CFLAGS% tools\make_icon.c /Fo"build\make_icon.obj" /Fe"build\make_icon.exe" /link ole32.lib windowscodecs.lib uuid.lib
if not "%errorlevel%"=="0" exit /b 1
build\make_icon.exe
if not "%errorlevel%"=="0" exit /b 1
rc /nologo /fo build\jotilla.res src\jotilla.rc
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% /c src\archive.c /Fo"build\archive.obj"
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% /c src\calendar.c /Fo"build\calendar.obj"
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% /c src\date.c /Fo"build\date.obj"
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% /c src\metadata.c /Fo"build\metadata.obj"
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% /c src\tags.c /Fo"build\tags.obj"
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% /c src\search.c /Fo"build\search.obj"
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% /c src\location.c /Fo"build\location.obj"
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% /c src\colors.c /Fo"build\colors.obj"
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% /c src\palette_picker.c /Fo"build\palette_picker.obj"
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% /c src\palette_art.c /Fo"build\palette_art.obj"
if not "%errorlevel%"=="0" exit /b 1
if /i "%~1"=="test-only" goto tests
if /i "%~1"=="screenshots-only" goto screenshots
if /i "%~1"=="profile-only" goto profile
if /i "%~1"=="search-profile" goto searchprofile
if /i "%~1"=="storage-profile" goto storageprofile
if /i "%~1"=="calendar-profile" goto calendarprofile
if /i "%~1"=="tags-profile" goto tagsprofile
if /i "%~1"=="test-checked" goto tests
cl %JOTILLA_CFLAGS% src\jotilla.c /Fo"build\jotilla.obj" /Fe"dist\Jotilla.exe" /link build\jotilla.res /SUBSYSTEM:WINDOWS %JOTILLA_MANIFEST% %JOTILLA_LIBS%
if not "%errorlevel%"=="0" exit /b 1
rc /nologo /fo build\setup.res src\setup.rc
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% src\setup.c /Fo"build\setup.obj" /Fe"dist\Jotilla-Setup-x64.exe" /link build\setup.res /SUBSYSTEM:WINDOWS %JOTILLA_MANIFEST% user32.lib shell32.lib ole32.lib uuid.lib advapi32.lib comctl32.lib
if not "%errorlevel%"=="0" exit /b 1
if /i "%~1"=="test" goto tests
if /i "%~1"=="screenshots" goto screenshots
echo Ready: %CD%\dist\Jotilla.exe
exit /b 0

:tests
cl %JOTILLA_CFLAGS% tests\tests.c /Fo"build\tests.obj" /Fe"build\tests.exe" /link build\jotilla.res /SUBSYSTEM:CONSOLE %JOTILLA_MANIFEST% %JOTILLA_LIBS%
if not "%errorlevel%"=="0" exit /b 1
build\tests.exe %~2
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% tests\setup_checks.c /Fo"build\setup_checks.obj" /Fe"build\setup_checks.exe" /link /SUBSYSTEM:CONSOLE %JOTILLA_MANIFEST% user32.lib shell32.lib ole32.lib uuid.lib advapi32.lib comctl32.lib
if not "%errorlevel%"=="0" exit /b 1
build\setup_checks.exe
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% tests\calendar_paint.c /Fo"build\calendar_paint.obj" /Fe"build\calendar_paint.exe" /link build\date.obj user32.lib gdi32.lib comctl32.lib %JOTILLA_MANIFEST%
if not "%errorlevel%"=="0" exit /b 1
build\calendar_paint.exe
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% tests\archive_cache.c /Fo"build\archive_cache.obj" /Fe"build\archive_cache.exe" /link %JOTILLA_MANIFEST% %JOTILLA_LIBS:build\archive.obj =%
if not "%errorlevel%"=="0" exit /b 1
build\archive_cache.exe
if not "%errorlevel%"=="0" exit /b 1
cl %JOTILLA_CFLAGS% tests\archive_layout.c /Fo"build\archive_layout.obj" /Fe"build\archive_layout.exe" /link %JOTILLA_MANIFEST% %JOTILLA_LIBS:build\archive.obj =%
if not "%errorlevel%"=="0" exit /b 1
build\archive_layout.exe
if not "%errorlevel%"=="0" exit /b 1
if not exist dist\Jotilla.exe exit /b 0
cl %JOTILLA_CFLAGS% tests\startup_checks.c /Fo"build\startup_checks.obj" /Fe"build\startup_checks.exe" /link user32.lib %JOTILLA_MANIFEST%
if not "%errorlevel%"=="0" exit /b 1
build\startup_checks.exe
exit /b %errorlevel%

:screenshots
cl %JOTILLA_CFLAGS% tests\demo.c /Fo"build\demo.obj" /Fe"build\demo.exe" /link build\jotilla.res /SUBSYSTEM:CONSOLE %JOTILLA_MANIFEST% %JOTILLA_LIBS%
if not "%errorlevel%"=="0" exit /b 1
build\demo.exe %~2
exit /b %errorlevel%

:profile
cl %JOTILLA_CFLAGS% tools\profile.c /Fo"build\profile.obj" /Fe"build\profile.exe" /link user32.lib psapi.lib
if not "%errorlevel%"=="0" exit /b 1
build\profile.exe "%CD%\dist\Jotilla.exe"
exit /b %errorlevel%

:searchprofile
cl %JOTILLA_CFLAGS% tools\search_profile.c /Fo"build\search_profile.obj" /Fe"build\search_profile.exe" /link build\search.obj build\tags.obj build\metadata.obj user32.lib normaliz.lib psapi.lib
if not "%errorlevel%"=="0" exit /b 1
build\search_profile.exe
exit /b %errorlevel%

:storageprofile
cl %JOTILLA_CFLAGS% tools\storage_profile.c /Fo"build\storage_profile.obj" /Fe"build\storage_profile.exe" /link build\jotilla.res /SUBSYSTEM:CONSOLE %JOTILLA_MANIFEST% %JOTILLA_LIBS%
if not "%errorlevel%"=="0" exit /b 1
build\storage_profile.exe
exit /b %errorlevel%

:calendarprofile
cl %JOTILLA_CFLAGS% tools\calendar_profile.c /Fo"build\calendar_profile.obj" /Fe"build\calendar_profile.exe" /link build\date.obj user32.lib gdi32.lib comctl32.lib %JOTILLA_MANIFEST%
if not "%errorlevel%"=="0" exit /b 1
build\calendar_profile.exe
exit /b %errorlevel%

:tagsprofile
cl %JOTILLA_CFLAGS% tools\tags_profile.c /Fo"build\tags_profile.obj" /Fe"build\tags_profile.exe" /link build\metadata.obj normaliz.lib
if not "%errorlevel%"=="0" exit /b 1
build\tags_profile.exe
exit /b %errorlevel%

:analyze
rem C6553 comes from the SDK's LCMapStringEx annotation, not application code.
rem The single search worker uses a bounded 26 KiB streaming buffer on its 1 MiB stack.
cl %JOTILLA_CFLAGS% /analyze /analyze:stacksize 32768 /wd6553 /c src\jotilla.c src\archive.c src\calendar.c src\date.c src\metadata.c src\tags.c src\search.c src\location.c src\colors.c src\palette_picker.c src\palette_art.c src\setup.c /Fo"build\\"
exit /b %errorlevel%

:compiler
set "JOTILLA_VS="
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo Install Microsoft Build Tools with C/C++ tools and the Windows SDK.
    exit /b 1
)
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "JOTILLA_VS=%%I"
if not defined JOTILLA_VS exit /b 1
call "%JOTILLA_VS%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
exit /b %errorlevel%
