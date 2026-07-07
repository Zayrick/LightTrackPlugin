cmake_minimum_required(VERSION 3.16)

project(LightTrackPlugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_library(LightTrackPlugin SHARED
  LightTrackPlugin_global.h
  LightTrackPlugin.cpp
  LightTrackPlugin.h
  third_party/miniaudio/miniaudio.c
  LightTrackEffectCatalog.h
  LightTrackEffectCatalog.cpp
)

target_compile_definitions(LightTrackPlugin PRIVATE LIGHTTRACKPLUGIN_LIBRARY)
target_compile_definitions(LightTrackPlugin PRIVATE MA_NO_DEVICE_IO MA_NO_ENCODING)
target_compile_definitions(LightTrackPlugin PRIVATE LIGHTTRACK_EFFECTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/OpenRGBEffectsPlugin/Effects")
target_include_directories(LightTrackPlugin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/OpenRGB
  ${CMAKE_CURRENT_SOURCE_DIR}/OpenRGB/i2c_smbus
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
)
target_link_libraries(LightTrackPlugin PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(WIN32)
  target_link_libraries(LightTrackPlugin PRIVATE winmm)
endif()
