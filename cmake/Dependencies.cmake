include_guard(GLOBAL)

function(lumora_find_dependencies)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets)
  find_package(OpenCV 4.10 REQUIRED COMPONENTS core imgproc imgcodecs)
  find_package(spdlog 1.15 REQUIRED CONFIG)

  if(LUMORA_BUILD_TESTS)
    find_package(GTest 1.15 REQUIRED CONFIG)
  endif()
endfunction()
