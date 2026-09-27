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
cl /nologo /std:c++20 /EHsc /W4 /MP /DUNICODE /D_UNICODE /DNOMINMAX /I "%CPPWINRT%" /Fo:"%BUILD%\\" "%ROOT%\native\src\probe\privacy-winrt.cpp" "%ROOT%\native\src\probe\privacy-native.cpp" /Fe:"%BUILD%\umbracam-privacy-probe.exe" /link windowsapp.lib runtimeobject.lib ole32.lib oleaut32.lib strmiids.lib setupapi.lib ksuser.lib
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /W4 /Fo:"%BUILD%\frame-statistics-test.obj" "%ROOT%\native\tests\frame-statistics-test.cpp" /Fe:"%BUILD%\frame-statistics-test.exe"
if errorlevel 1 exit /b %errorlevel%
"%BUILD%\frame-statistics-test.exe"
exit /b %errorlevel%
