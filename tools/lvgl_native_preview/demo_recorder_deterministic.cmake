# Runs a scene twice and fails unless the recorder reports the same digest of every frame the
# firmware drew: a scene must render identically each time, whatever the machine or its load.
#
#   cmake -DRECORDER=<cyd_demo_recorder> -DSCENE=<scene.scn> -P demo_recorder_deterministic.cmake
foreach(run IN ITEMS 1 2)
  execute_process(
    COMMAND "${RECORDER}" "${SCENE}" --digest
    RESULT_VARIABLE result_${run}
    ERROR_VARIABLE log_${run}
    OUTPUT_QUIET)
  if(NOT result_${run} EQUAL 0)
    message(FATAL_ERROR "run ${run} failed with ${result_${run}}:\n${log_${run}}")
  endif()
  string(REGEX MATCH "digest=[0-9a-f]+" digest_${run} "${log_${run}}")
  if(NOT digest_${run})
    message(FATAL_ERROR "run ${run} printed no digest:\n${log_${run}}")
  endif()
endforeach()

if(NOT digest_1 STREQUAL digest_2)
  message(FATAL_ERROR "the scene rendered differently twice: ${digest_1}, then ${digest_2}")
endif()
message(STATUS "identical frames both times: ${digest_1}")
