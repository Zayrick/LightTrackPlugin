cmake_minimum_required(VERSION 3.16)

project(LightTrackPlugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets OpenGL)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets OpenGL)
if(QT_VERSION_MAJOR EQUAL 6)
  find_package(Qt6 REQUIRED COMPONENTS Core5Compat OpenGLWidgets)
endif()

set(OPENRGB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/OpenRGB)
set(OPENRGB_EFFECTS_PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/OpenRGBEffectsPlugin)
set(MINIAUDIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/miniaudio)
file(GLOB_RECURSE OPENRGB_EFFECT_SOURCES CONFIGURE_DEPENDS
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/*.cpp
)
file(GLOB_RECURSE OPENRGB_EFFECT_FORMS CONFIGURE_DEPENDS
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/*.ui
)
file(GLOB OPENRGB_QCODEEDITOR_SOURCES CONFIGURE_DEPENDS
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/src/internal/*.cpp
)
file(GLOB OPENRGB_QCODEEDITOR_HEADERS CONFIGURE_DEPENDS
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/include/internal/*.hpp
)
set(OPENRGB_EFFECT_MANUAL_MOC_HEADERS
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Policing/Policing.h
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Visor/Visor.h
)
set_source_files_properties(
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Policing/Policing.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Visor/Visor.cpp
  ${OPENRGB_EFFECT_MANUAL_MOC_HEADERS}
  PROPERTIES SKIP_AUTOMOC ON
)
if(QT_VERSION_MAJOR EQUAL 6)
  qt6_wrap_cpp(OPENRGB_EFFECT_MANUAL_MOC ${OPENRGB_EFFECT_MANUAL_MOC_HEADERS})
  qt6_wrap_ui(OPENRGB_EFFECT_MANUAL_UI ${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectTab.ui)
else()
  qt5_wrap_cpp(OPENRGB_EFFECT_MANUAL_MOC ${OPENRGB_EFFECT_MANUAL_MOC_HEADERS})
  qt5_wrap_ui(OPENRGB_EFFECT_MANUAL_UI ${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectTab.ui)
endif()
set(OPENRGB_EFFECT_SUPPORT_FORMS
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSettings.ui
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/ColorPicker.ui
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/ColorsPicker.ui
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/EffectList.ui
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/EffectSearch.ui
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/LivePreviewController.ui
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectTab.ui
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectPage.ui
)
set(OPENRGB_AUTOUIC_SEARCH_PATHS ${OPENRGB_EFFECT_SUPPORT_FORMS} ${OPENRGB_EFFECT_FORMS})
foreach(form ${OPENRGB_AUTOUIC_SEARCH_PATHS})
  get_filename_component(form_dir ${form} DIRECTORY)
  list(APPEND OPENRGB_EFFECT_UI_DIRS ${form_dir})
endforeach()
list(REMOVE_DUPLICATES OPENRGB_EFFECT_UI_DIRS)

add_library(LightTrackPlugin SHARED
  LightTrackPlugin_global.h
  LightTrackPlugin.cpp
  LightTrackPlugin.h
  ${MINIAUDIO_DIR}/miniaudio.c
  LightTrackEffectCatalog.h
  LightTrackEffectCatalog.cpp
  LightTrackOpenRGBEffectsBridge.h
  LightTrackOpenRGBEffectsBridge.cpp
  ${OPENRGB_DIR}/RGBController/RGBController.cpp
  ${OPENRGB_DIR}/qt/hsv.cpp
  ${OPENRGB_DIR}/qt/QTooltipedSlider.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/EffectListManager.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/EffectList.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/EffectManager.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/EffectSearch.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/LivePreviewController.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectSettings.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectPage.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBPluginsFont.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/PreviewWidget.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioManager.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSettings.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSettingsStruct.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSignalProcessor.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/ColorPicker.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/ColorsPicker.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/qt/QtScreenCapturer.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/windows/WindowsScreenCapturer.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/ScreenCapturer.h
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/RGBEffect.h
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/chuck_fft/chuck_fft.c
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/ctkrangeslider/ctkrangeslider.cpp
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/SimplexNoise/src/SimplexNoise.cpp
  ${OPENRGB_QCODEEDITOR_SOURCES}
  ${OPENRGB_QCODEEDITOR_HEADERS}
  ${OPENRGB_EFFECT_MANUAL_MOC}
  ${OPENRGB_EFFECT_MANUAL_UI}
  ${OPENRGB_EFFECT_SOURCES}
  ${OPENRGB_EFFECT_FORMS}
  ${OPENRGB_EFFECT_SUPPORT_FORMS}
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/resources.qrc
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/resources/qcodeeditor_resources.qrc
)

target_compile_definitions(LightTrackPlugin PRIVATE LIGHTTRACKPLUGIN_LIBRARY)
target_compile_definitions(LightTrackPlugin PRIVATE MA_NO_ENCODING)
target_compile_definitions(LightTrackPlugin PRIVATE SHADERS_README="https://gitlab.com/OpenRGBDevelopers/OpenRGBEffectsPlugin/-/blob/master/Effects/Shaders/README.md")
set_target_properties(LightTrackPlugin PROPERTIES AUTOUIC_SEARCH_PATHS "${OPENRGB_EFFECT_UI_DIRS}")
target_include_directories(LightTrackPlugin PRIVATE
  ${OPENRGB_DIR}
  ${OPENRGB_DIR}/RGBController
  ${OPENRGB_DIR}/dependencies/json
  ${OPENRGB_DIR}/i2c_smbus
  ${OPENRGB_DIR}/net_port
  ${OPENRGB_DIR}/qt
  ${CMAKE_CURRENT_BINARY_DIR}
  ${MINIAUDIO_DIR}
  ${OPENRGB_EFFECTS_PLUGIN_DIR}
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/qt
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/windows
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/chuck_fft
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/ctkrangeslider
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/SimplexNoise/src
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/include
  ${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/include/internal
)
target_link_libraries(LightTrackPlugin PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::OpenGL)
if(QT_VERSION_MAJOR EQUAL 6)
  target_link_libraries(LightTrackPlugin PRIVATE Qt6::Core5Compat Qt6::OpenGLWidgets)
endif()

if(WIN32)
  target_compile_definitions(LightTrackPlugin PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS WIN32_LEAN_AND_MEAN)
  target_link_libraries(LightTrackPlugin PRIVATE winmm ole32 user32 gdi32 opengl32)
endif()

if(MSVC)
  target_compile_options(LightTrackPlugin PRIVATE /FS)
endif()
