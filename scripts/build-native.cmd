@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build"
set "TOOLCHAIN=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "CPPWINRT=C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt"

if not exist "%TOOLCHAIN%" (
  echo Visual Studio C++ Build Tools were not found.
  exit /b 1
)
if not exist "%CPPWINRT%" (
  echo Windows 11 SDK C++/WinRT headers were not found.
  exit /b 1
)

if not exist "%BUILD%" mkdir "%BUILD%"
call "%TOOLCHAIN%" >nul

cl /nologo /std:c++20 /EHsc /I "%ROOT%\native\include" /Fo:"%BUILD%\ir_camera-host.obj" "%ROOT%\native\src\host\main.cpp" /Fe:"%BUILD%\ir_camera-host.exe" /link mfplat.lib mfuuid.lib mfsensorgroup.lib ole32.lib advapi32.lib runtimeobject.lib
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /LD /I "%ROOT%\native\include" /I "%ROOT%\native\src\dll" /Fo:"%BUILD%\\" /Fd:"%BUILD%\IRCameraBridgeVCam.pdb" "%ROOT%\native\src\dll\Activator.cpp" "%ROOT%\native\src\dll\dllmain.cpp" "%ROOT%\native\src\dll\FrameSource.cpp" "%ROOT%\native\src\dll\MediaSource.cpp" "%ROOT%\native\src\dll\MediaStream.cpp" /Fe:"%BUILD%\IRCameraBridgeVCam.dll" /link /DEF:"%ROOT%\native\src\dll\IRCameraBridgeVCam.def" mfplat.lib mfuuid.lib mfsensorgroup.lib ole32.lib oleaut32.lib advapi32.lib runtimeobject.lib
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /I "%ROOT%\native\include" /I "%CPPWINRT%" /Fo:"%BUILD%\ir_camera-bridge.obj" "%ROOT%\native\src\bridge\main.cpp" /Fe:"%BUILD%\ir_camera-bridge.exe" /link windowsapp.lib runtimeobject.lib shell32.lib user32.lib
exit /b %errorlevel%
