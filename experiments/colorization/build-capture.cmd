@echo off
setlocal
set "ROOT=%~dp0..\.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /I "%ROOT%\native\include" /I "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt" /Fo:"%ROOT%\build\capture-clip.obj" "%~dp0capture-clip.cpp" /Fe:"%ROOT%\build\capture-clip.exe" /link windowsapp.lib runtimeobject.lib mf.lib mfplat.lib mfuuid.lib ole32.lib
exit /b %errorlevel%
