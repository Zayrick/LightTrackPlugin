include_guard(GLOBAL)

include(CMakeParseArguments)

function(lighttrack_configure_openrgb_effects)
  set(one_value_args
    TARGET
    OPENRGB_DIR
    OPENRGB_EFFECTS_PLUGIN_DIR
  )
  cmake_parse_arguments(LIGHTTRACK "" "${one_value_args}" "" ${ARGN})

  if(NOT LIGHTTRACK_TARGET)
    message(FATAL_ERROR "lighttrack_configure_openrgb_effects requires TARGET")
  endif()
  if(NOT TARGET "${LIGHTTRACK_TARGET}")
    message(FATAL_ERROR
      "lighttrack_configure_openrgb_effects target does not exist: ${LIGHTTRACK_TARGET}"
    )
  endif()
  if(NOT LIGHTTRACK_OPENRGB_DIR)
    message(FATAL_ERROR "lighttrack_configure_openrgb_effects requires OPENRGB_DIR")
  endif()
  if(NOT LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR)
    message(FATAL_ERROR
      "lighttrack_configure_openrgb_effects requires OPENRGB_EFFECTS_PLUGIN_DIR"
    )
  endif()
  if(LIGHTTRACK_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "Unknown lighttrack_configure_openrgb_effects arguments: "
      "${LIGHTTRACK_UNPARSED_ARGUMENTS}"
    )
  endif()
  if(NOT DEFINED QT_VERSION_MAJOR)
    message(FATAL_ERROR
      "Find Qt before calling lighttrack_configure_openrgb_effects"
    )
  endif()

  if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Core5Compat OpenGLWidgets)
  endif()

  file(GLOB_RECURSE openrgb_effect_sources CONFIGURE_DEPENDS
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/*.cpp"
  )
  file(GLOB_RECURSE openrgb_effect_forms CONFIGURE_DEPENDS
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/*.ui"
  )
  file(GLOB openrgb_qcodeeditor_sources CONFIGURE_DEPENDS
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/src/internal/*.cpp"
  )
  file(GLOB openrgb_qcodeeditor_headers CONFIGURE_DEPENDS
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/include/internal/*.hpp"
  )

  set(openrgb_effect_manual_moc_headers
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Policing/Policing.h"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Visor/Visor.h"
  )
  set_source_files_properties(
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Policing/Policing.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/Visor/Visor.cpp"
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
      "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectTab.ui"
    )
  else()
    qt5_wrap_cpp(
      openrgb_effect_manual_moc
      ${openrgb_effect_manual_moc_headers}
    )
    qt5_wrap_ui(
      openrgb_effect_manual_ui
      "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectTab.ui"
    )
  endif()

  set(openrgb_effect_support_forms
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSettings.ui"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/ColorPicker.ui"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/ColorsPicker.ui"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/EffectList.ui"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/EffectSearch.ui"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/LivePreviewController.ui"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectTab.ui"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectPage.ui"
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

  target_sources("${LIGHTTRACK_TARGET}" PRIVATE
    "${LIGHTTRACK_OPENRGB_DIR}/RGBController/RGBController.cpp"
    "${LIGHTTRACK_OPENRGB_DIR}/qt/hsv.cpp"
    "${LIGHTTRACK_OPENRGB_DIR}/qt/QTooltipedSlider.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/EffectListManager.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/EffectList.cpp"
    # EffectManager.cpp is replaced by OpenRgbEffectManager.cpp in the owning
    # target so timeline writes and effect frames share one synchronization path.
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/EffectSearch.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/LivePreviewController.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectSettings.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBEffectPage.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/OpenRGBPluginsFont.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/PreviewWidget.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioManager.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSettings.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSettingsStruct.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Audio/AudioSignalProcessor.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/ColorPicker.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/ColorsPicker.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/qt/QtScreenCapturer.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/windows/WindowsScreenCapturer.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/ScreenCapturer.h"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/RGBEffect.h"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/chuck_fft/chuck_fft.c"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/ctkrangeslider/ctkrangeslider.cpp"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/SimplexNoise/src/SimplexNoise.cpp"
    ${openrgb_qcodeeditor_sources}
    ${openrgb_qcodeeditor_headers}
    ${openrgb_effect_manual_moc}
    ${openrgb_effect_manual_ui}
    ${openrgb_effect_sources}
    ${openrgb_effect_forms}
    ${openrgb_effect_support_forms}
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/resources.qrc"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/resources/qcodeeditor_resources.qrc"
  )

  set_target_properties("${LIGHTTRACK_TARGET}" PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${openrgb_effect_ui_dirs}"
    POSITION_INDEPENDENT_CODE ON
  )

  target_compile_definitions("${LIGHTTRACK_TARGET}" PRIVATE
    SHADERS_README="https://gitlab.com/OpenRGBDevelopers/OpenRGBEffectsPlugin/-/blob/master/Effects/Shaders/README.md"
  )

  # The final host adapter includes OpenRGBPluginInterface.h. Only the SDK
  # paths needed by that public header are propagated beyond this object target.
  target_include_directories("${LIGHTTRACK_TARGET}" PUBLIC
    "${LIGHTTRACK_OPENRGB_DIR}"
    "${LIGHTTRACK_OPENRGB_DIR}/i2c_smbus"
  )

  target_include_directories("${LIGHTTRACK_TARGET}" PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${LIGHTTRACK_OPENRGB_DIR}/RGBController"
    "${LIGHTTRACK_OPENRGB_DIR}/dependencies/json"
    "${LIGHTTRACK_OPENRGB_DIR}/net_port"
    "${LIGHTTRACK_OPENRGB_DIR}/qt"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Audio"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Effects"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/qt"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/ScreenCapturer/windows"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/chuck_fft"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/ctkrangeslider"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/SimplexNoise/src"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/include"
    "${LIGHTTRACK_OPENRGB_EFFECTS_PLUGIN_DIR}/Dependencies/QCodeEditor/include/internal"
  )

  # Object libraries do not have a link step. Their link requirements must be
  # public so the final shared-library consumer resolves the generated objects.
  target_link_libraries("${LIGHTTRACK_TARGET}" PUBLIC
    "Qt${QT_VERSION_MAJOR}::Widgets"
    "Qt${QT_VERSION_MAJOR}::OpenGL"
  )
  if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries("${LIGHTTRACK_TARGET}" PUBLIC
      Qt6::Core5Compat
      Qt6::OpenGLWidgets
    )
  endif()

  if(WIN32)
    target_compile_definitions("${LIGHTTRACK_TARGET}" PRIVATE
      _CRT_SECURE_NO_WARNINGS
      _WINSOCK_DEPRECATED_NO_WARNINGS
      WIN32_LEAN_AND_MEAN
    )
    target_link_libraries("${LIGHTTRACK_TARGET}" PUBLIC
      winmm
      ole32
      user32
      gdi32
      opengl32
    )
  endif()

  if(MSVC)
    target_compile_options("${LIGHTTRACK_TARGET}" PRIVATE /FS)
  endif()
endfunction()
