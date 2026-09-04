function(require_target name)
  if(NOT TARGET ${name})
    message(FATAL_ERROR "Required target missing: ${name}")
  endif()
endfunction()

function(lumora_verify_milestone_one_targets)
  require_target(lumora_core)
  require_target(lumora_configuration)
  require_target(lumora_diagnostics)
  require_target(lumora_ui)
  require_target(lumora_app)

  if(TARGET lumora_camera_basler AND NOT LUMORA_ENABLE_BASLER)
    message(FATAL_ERROR
      "lumora_camera_basler must not exist when LUMORA_ENABLE_BASLER is OFF")
  endif()
endfunction()
