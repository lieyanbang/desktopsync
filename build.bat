@echo off
setlocal
set VS_BAT=%VS_BAT%
if "%VS_BAT%"=="" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" (
        for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%~I"
        if defined VS_INSTALL (
            set "VS_BAT=%VS_INSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
        )
    )
)
if not defined VS_BAT (
    set "VS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
)
if not exist "%VS_BAT%" (
    echo Visual Studio vcvarsall.bat not found: %VS_BAT%
    exit /b 1
)
call "%VS_BAT%" x64 || exit /b 1
cd /d %~dp0
set CL_COMMON=/std:c++20 /O2 /EHsc /DWIN32 /D_WINDOWS /DWIN64 /D_WIN32_WINNT=0x0601
set LIBS_WIN=Shell32.lib Shlwapi.lib Ole32.lib OleAut32.lib User32.lib Gdi32.lib Advapi32.lib
set LIBS_WS=Ws2_32.lib

del /q *.obj 2>nul
for %%f in (desktopsync.exe config.dll p2p.dll trayhook.dll sync.dll config.lib p2p.lib trayhook.lib sync.lib config.exp p2p.exp trayhook.exp sync.exp) do if exist %%f del %%f

cl %CL_COMMON% /LD src\config_dll.cpp src\common.cpp /I src /Fe:config.dll Shell32.lib Shlwapi.lib Ole32.lib OleAut32.lib User32.lib Gdi32.lib || goto :err
cl %CL_COMMON% /LD src\p2p_dll.cpp src\common.cpp /I src /Fe:p2p.dll Ws2_32.lib Shell32.lib || goto :err
cl %CL_COMMON% /LD src\trayhook_dll.cpp src\common.cpp /I src /Fe:trayhook.dll Shell32.lib Shlwapi.lib User32.lib Gdi32.lib || goto :err
cl %CL_COMMON% /c src\common.cpp /I src /Fo:ds_common.obj || goto :err
cl %CL_COMMON% /LD src\sync_dll.cpp udt4\src\*.cpp ds_common.obj /I src /I udt4\src /DUDT_EXPORTS /Fe:sync.dll Ws2_32.lib Shell32.lib Shlwapi.lib User32.lib Gdi32.lib Advapi32.lib || goto :err
cl %CL_COMMON% src\desktopsync_main.cpp ds_common.obj /I src /Fe:desktopsync.exe config.lib p2p.lib trayhook.lib sync.lib Ws2_32.lib Shell32.lib Shlwapi.lib Ole32.lib OleAut32.lib User32.lib Gdi32.lib Advapi32.lib || goto :err

echo Build completed successfully.
exit /b 0
:err
echo Build failed.
exit /b 1
