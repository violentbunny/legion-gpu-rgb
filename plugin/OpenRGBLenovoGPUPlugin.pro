#-----------------------------------------------------------------------------#
# OpenRGBLenovoGPUPlugin.pro                                                   #
#                                                                             #
# TARGET: OpenRGB 1.0rc3 / 1.0rc3.1, plugin API 4, Qt 5.15.0, MSVC.          #
#                                                                             #
# Build with MSVC, NOT MinGW. Qt plugins are ABI-coupled to the host app, and #
# official OpenRGB releases are MSVC builds - a MinGW-built DLL will not load. #
#                                                                             #
#   qmake OPENRGB_PATH=C:/path/to/OpenRGB OpenRGBLenovoGPUPlugin.pro           #
#   nmake                                                                      #
#-----------------------------------------------------------------------------#

QT += core gui widgets
TEMPLATE = lib
CONFIG  += plugin c++17
TARGET   = OpenRGBLenovoGPUPlugin
DEFINES += LENOVOGPUPLUGIN_LIBRARY

isEmpty(OPENRGB_PATH) {
    OPENRGB_PATH = $$PWD/../OpenRGB
}

!exists($$OPENRGB_PATH/OpenRGBPluginInterface.h) {
    error("OpenRGB source not found at $$OPENRGB_PATH - pass OPENRGB_PATH=<dir> to qmake")
}

#-----------------------------------------------------------------------------#
# OpenRGB's own ResourceManagerInterface.h includes i2c_smbus.h, so i2c_smbus  #
# has to be on the include path even though this plugin never uses OpenRGB's   #
# I2C layer. Resolved by walking the include graph of the three headers this   #
# plugin pulls in - these three directories are sufficient and complete.       #
#-----------------------------------------------------------------------------#
INCLUDEPATH +=                                  \
    $$OPENRGB_PATH                              \
    $$OPENRGB_PATH/RGBController                \
    $$OPENRGB_PATH/i2c_smbus

HEADERS +=                                      \
    LenovoGPUNvAPI.h                            \
    LenovoGPUController.h                       \
    RGBController_LenovoGPU.h                   \
    RGBController_LenovoGPULogo.h               \
    LenovoGPUPlugin.h

SOURCES +=                                      \
    LenovoGPUNvAPI.cpp                          \
    LenovoGPUController.cpp                     \
    RGBController_LenovoGPU.cpp                 \
    RGBController_LenovoGPULogo.cpp             \
    LenovoGPUPlugin.cpp

#-----------------------------------------------------------------------------#
# RGBController.cpp implements the base class this plugin subclasses - the     #
# non-pure virtuals, plus the mode/zone/led constructors. Without it the link  #
# fails with ~44 unresolved externals. It is self-contained: its only includes #
# are <cstring> and RGBController.h, so nothing else is dragged in.            #
#-----------------------------------------------------------------------------#
SOURCES +=                                      \
    $$OPENRGB_PATH/RGBController/RGBController.cpp

#-----------------------------------------------------------------------------#
# nvapi64.dll is resolved at runtime with LoadLibrary, so there is no import   #
# library to link and no NVIDIA SDK required to build.                        #
#-----------------------------------------------------------------------------#
win32:LIBS += -lkernel32

#-----------------------------------------------------------------------------#
# The controller applies state on a worker thread so callers that stream frames #
# (the OpenRGB SDK, and SignalRGB through the bridge) are never blocked by a    #
# 555 ms bus burst. MSVC needs nothing extra for std::thread; Linux does.       #
#-----------------------------------------------------------------------------#
unix:LIBS += -lpthread
