add_library(IRCameraBridgeVCam SHARED
    dllmain.cpp
    Activator.cpp
    MediaSource.cpp
    MediaStream.cpp
    FrameSource.cpp
    IRCameraBridgeVCam.def
)
target_include_directories(IRCameraBridgeVCam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_link_libraries(IRCameraBridgeVCam PRIVATE
    mfplat mfuuid mfsensorgroup ole32 oleaut32 advapi32 runtimeobject
)
set_target_properties(IRCameraBridgeVCam PROPERTIES PREFIX "")

