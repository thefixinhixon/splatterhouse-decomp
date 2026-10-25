# Codegen guard for bootstrap without the XEX.
# Local patch over generated/rexglue.cmake: if the XEX is not present,
# codegen is skipped (bootstrap project) instead of failing the whole build.
# Arguments: -DMANIFEST= -DXEX= -DCODEGEN_D= -DREXGLUE_EXE=

if(NOT EXISTS "${XEX}")
  message(STATUS "[splatterhouse] XEX not present: ${XEX} - codegen skipped (bootstrap mode).")
  # Empty depfile so the later transform doesn't fail.
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../generated")
  file(WRITE "${CODEGEN_D}" "")
else()
  execute_process(COMMAND "${REXGLUE_EXE}" codegen "${MANIFEST}" RESULT_VARIABLE res)
  if(NOT res EQUAL 0)
    message(FATAL_ERROR "[splatterhouse] rexglue codegen failed (use --force in rexglue to continue despite validation errors)")
  endif()
endif()
