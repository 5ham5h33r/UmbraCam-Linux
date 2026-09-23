add_library(G14IrBridgeVCam SHARED
    dllmain.cpp
    Activator.cpp
    MediaSource.cpp
    MediaStream.cpp
    FrameSource.cpp
    G14IrBridgeVCam.def
)
target_include_directories(G14IrBridgeVCam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_link_libraries(G14IrBridgeVCam PRIVATE
    mfplat mfuuid mfsensorgroup ole32 oleaut32 advapi32 runtimeobject
)
set_target_properties(G14IrBridgeVCam PROPERTIES PREFIX "")

