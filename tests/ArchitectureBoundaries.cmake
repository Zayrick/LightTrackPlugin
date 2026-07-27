if(NOT DEFINED LIGHTTRACK_SOURCE_DIR)
  message(FATAL_ERROR "LIGHTTRACK_SOURCE_DIR is required")
endif()

function(lighttrack_assert_files_exclude description)
  set(multi_value_args PATTERNS FORBIDDEN)
  cmake_parse_arguments(
    CHECK
    ""
    ""
    "${multi_value_args}"
    ${ARGN}
  )

  file(GLOB_RECURSE checked_files
    LIST_DIRECTORIES false
    ${CHECK_PATTERNS}
  )

  foreach(checked_file IN LISTS checked_files)
    file(READ "${checked_file}" contents)
    foreach(forbidden_text IN LISTS CHECK_FORBIDDEN)
      string(FIND "${contents}" "${forbidden_text}" match_position)
      if(NOT match_position EQUAL -1)
        file(RELATIVE_PATH relative_file
          "${LIGHTTRACK_SOURCE_DIR}"
          "${checked_file}"
        )
        message(FATAL_ERROR
          "${description}: ${relative_file} contains forbidden dependency "
          "\"${forbidden_text}\""
        )
      endif()
    endforeach()
  endforeach()
endfunction()

lighttrack_assert_files_exclude(
  "UI must depend on application ports instead of concrete adapters"
  PATTERNS
    "${LIGHTTRACK_SOURCE_DIR}/src/ui/*.h"
    "${LIGHTTRACK_SOURCE_DIR}/src/ui/*.cpp"
  FORBIDDEN
    "\"integrations/"
    "\"infrastructure/"
    "nlohmann"
    "miniaudio"
    "RGBController"
    "RGBEffect"
    "ResourceManagerInterface"
)

lighttrack_assert_files_exclude(
  "Application ports must not depend on outer layers or vendor APIs"
  PATTERNS
    "${LIGHTTRACK_SOURCE_DIR}/src/application/*.h"
    "${LIGHTTRACK_SOURCE_DIR}/src/application/*.cpp"
  FORBIDDEN
    "\"integrations/"
    "\"infrastructure/"
    "\"ui/"
    "nlohmann"
    "miniaudio"
    "RGBController"
    "RGBEffect"
    "ResourceManagerInterface"
)

lighttrack_assert_files_exclude(
  "Core must remain independent from UI, I/O, and vendor APIs"
  PATTERNS
    "${LIGHTTRACK_SOURCE_DIR}/src/core/*.h"
    "${LIGHTTRACK_SOURCE_DIR}/src/core/*.cpp"
  FORBIDDEN
    "\"application/"
    "\"integrations/"
    "\"infrastructure/"
    "\"ui/"
    "QWidget"
    "QGraphics"
    "nlohmann"
    "miniaudio"
    "RGBController"
    "RGBEffect"
    "ResourceManagerInterface"
)
