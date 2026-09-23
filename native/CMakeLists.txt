cmake_minimum_required(VERSION 3.21)
project(g14ir-camera-bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# No WIN32_LEAN_AND_MEAN: ksproxy.h needs the full OLE headers.
add_compile_definitions(UNICODE _UNICODE NOMINMAX)

# Host: registers the session-lifetime virtual camera and keeps it alive.
add_executable(g14ir-host src/host/main.cpp)
target_include_directories(g14ir-host PRIVATE include)
target_link_libraries(g14ir-host PRIVATE mfplat mfuuid mfsensorgroup ole32 advapi32 runtimeobject)

# Companion process: requests camera consent, reads the Windows Hello IR stream,
# and publishes BGRA frames to the media source's shared-memory transport.
add_executable(g14ir-bridge WIN32 src/bridge/main.cpp)
target_include_directories(g14ir-bridge PRIVATE include)
target_link_libraries(g14ir-bridge PRIVATE windowsapp runtimeobject shell32 user32)

# COM media source loaded by the Windows Frame Server.
option(G14IR_BUILD_DLL "Build the G14 IR Camera media-source DLL" ON)
if(G14IR_BUILD_DLL)
    add_subdirectory(src/dll)
endif()
