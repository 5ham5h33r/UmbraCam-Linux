@echo off
setlocal
set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build"
set "TOOLCHAIN=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "CPPWINRT=C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt"
if not exist "%TOOLCHAIN%" exit /b 1
if not exist "%BUILD%" mkdir "%BUILD%"
call "%TOOLCHAIN%" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /W4 /DUNICODE /D_UNICODE /DNOMINMAX /I "%CPPWINRT%" /Fo:"%BUILD%\faceauth-probe.obj" "%ROOT%\experiments\capture\compare-faceauth.cpp" /Fe:"%BUILD%\umbracam-faceauth-probe.exe" /link windowsapp.lib runtimeobject.lib ole32.lib oleaut32.lib strmiids.lib setupapi.lib ksuser.lib mf.lib mfplat.lib mfuuid.lib mfreadwrite.lib
exit /b %errorlevel%
