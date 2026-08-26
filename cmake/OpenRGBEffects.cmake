include_guard(GLOBAL)

function(lighttrack_configure_openrgb_effects target)
  if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Core5Compat OpenGLWidgets)
  endif()

  file(GLOB_RECURSE openrgb_effect_sources CONFIGURE_DEPENDS
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/*.cpp"
  )
  file(GLOB_RECURSE openrgb_effect_forms CONFIGURE_DEPENDS
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/*.ui"
  )
  file(GLOB openrgb_qcodeeditor_sources CONFIGURE_DEPENDS
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/src/internal/*.cpp"
  )
  file(GLOB openrgb_qcodeeditor_headers CONFIGURE_DEPENDS
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/include/internal/*.hpp"
  )

  set(openrgb_effect_manual_moc_headers
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Policing/Policing.h"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Visor/Visor.h"
  )
  set_source_files_properties(
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Policing/Policing.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Visor/Visor.cpp"
    ${openrgb_effect_manual_moc_headers}
    PROPERTIES SKIP_AUTOMOC ON
  )

  if(QT_VERSION_MAJOR EQUAL 6)
    qt6_wrap_cpp(
      openrgb_effect_manual_moc
      ${openrgb_effect_manual_moc_headers}
    )
    qt6_wrap_ui(
      openrgb_effect_manual_ui
      "${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectTab.ui"
    )
  else()
    qt5_wrap_cpp(
      openrgb_effect_manual_moc
      ${openrgb_effect_manual_moc_headers}
    )
    qt5_wrap_ui(
      openrgb_effect_manual_ui
      "${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectTab.ui"
    )
  endif()

  set(openrgb_effect_support_forms
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSettings.ui"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/ColorPicker.ui"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/ColorsPicker.ui"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/EffectList.ui"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/EffectSearch.ui"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/LivePreviewController.ui"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectTab.ui"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectPage.ui"
  )
  set(openrgb_autouic_search_paths
    ${openrgb_effect_support_forms}
    ${openrgb_effect_forms}
  )
  set(openrgb_effect_ui_dirs)
  foreach(form IN LISTS openrgb_autouic_search_paths)
    get_filename_component(form_dir "${form}" DIRECTORY)
    list(APPEND openrgb_effect_ui_dirs "${form_dir}")
  endforeach()
  list(REMOVE_DUPLICATES openrgb_effect_ui_dirs)

  target_sources("${target}" PRIVATE
    "${OPENRGB_DIR}/RGBController/RGBController.cpp"
    "${OPENRGB_DIR}/qt/hsv.cpp"
    "${OPENRGB_DIR}/qt/QTooltipedSlider.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/EffectListManager.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/EffectList.cpp"
    # EffectManager.cpp is replaced by OpenRgbEffectManager.cpp in the owning
    # target so timeline writes and effect frames share one synchronization path.
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/EffectSearch.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/LivePreviewController.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectSettings.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectPage.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBPluginsFont.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/PreviewWidget.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioManager.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSettings.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSettingsStruct.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSignalProcessor.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/ColorPicker.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/ColorsPicker.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/qt/QtScreenCapturer.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/windows/WindowsScreenCapturer.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/ScreenCapturer.h"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/RGBEffect.h"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/chuck_fft/chuck_fft.c"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/ctkrangeslider/ctkrangeslider.cpp"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/SimplexNoise/src/SimplexNoise.cpp"
    ${openrgb_qcodeeditor_sources}
    ${openrgb_qcodeeditor_headers}
    ${openrgb_effect_manual_moc}
    ${openrgb_effect_manual_ui}
    ${openrgb_effect_sources}
    ${openrgb_effect_forms}
    ${openrgb_effect_support_forms}
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/resources.qrc"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/resources/qcodeeditor_resources.qrc"
  )

  set_target_properties("${target}" PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${openrgb_effect_ui_dirs}"
  )

  target_compile_definitions("${target}" PRIVATE
    SHADERS_README="https://gitlab.com/OpenRGBDevelopers/OpenRGBEffectsPlugin/-/blob/master/Effects/Shaders/README.md"
  )

  target_include_directories("${target}" PRIVATE
    "${OPENRGB_DIR}"
    "${OPENRGB_DIR}/RGBController"
    "${OPENRGB_DIR}/dependencies/json"
    "${OPENRGB_DIR}/i2c_smbus"
    "${OPENRGB_DIR}/net_port"
    "${OPENRGB_DIR}/qt"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Audio"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/qt"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/windows"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/chuck_fft"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/ctkrangeslider"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/SimplexNoise/src"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/include"
    "${OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/include/internal"
  )

  target_link_libraries("${target}" PRIVATE
    "Qt${QT_VERSION_MAJOR}::Widgets"
    "Qt${QT_VERSION_MAJOR}::OpenGL"
  )
  if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries("${target}" PRIVATE
      Qt6::Core5Compat
      Qt6::OpenGLWidgets
    )
  endif()

  if(WIN32)
    target_compile_definitions("${target}" PRIVATE
      _CRT_SECURE_NO_WARNINGS
      _WINSOCK_DEPRECATED_NO_WARNINGS
      WIN32_LEAN_AND_MEAN
    )
    target_link_libraries("${target}" PRIVATE
      winmm
      ole32
      user32
      gdi32
      opengl32
    )
  endif()

  if(MSVC)
    target_compile_options("${target}" PRIVATE /FS)
  endif()
endfunction()
