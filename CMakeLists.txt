cmake_minimum_required(VERSION 3.16)

project(LightTrackPlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_library(LightTrackPlugin SHARED
  LightTrackPlugin_global.h
  LightTrackPlugin.cpp
  LightTrackPlugin.h
)

target_compile_definitions(LightTrackPlugin PRIVATE LIGHTTRACKPLUGIN_LIBRARY)
target_include_directories(LightTrackPlugin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/OpenRGB
  ${CMAKE_CURRENT_SOURCE_DIR}/OpenRGB/i2c_smbus
)
target_link_libraries(LightTrackPlugin PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
