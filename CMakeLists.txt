cmake_minimum_required(VERSION 3.16)

project(LightTrackPlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(LightTrackPlugin SHARED
  LightTrackPlugin_global.h
  LightTrackPlugin.cpp
  LightTrackPlugin.h
)

target_compile_definitions(LightTrackPlugin PRIVATE LIGHTTRACKPLUGIN_LIBRARY)
