include_guard(GLOBAL)

function(lumora_configure_pylon)
  if(NOT LUMORA_ENABLE_BASLER)
    message(STATUS "Basler pylon support is disabled")
    return()
  endif()

  if(NOT WIN32)
    message(FATAL_ERROR "Basler pylon builds are supported only on Windows")
  endif()

  find_package(pylon CONFIG REQUIRED)
endfunction()
