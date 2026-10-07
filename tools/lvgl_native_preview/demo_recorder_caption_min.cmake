# --min-caption pauses a scene so each caption can be read; `caption-min` in a scene replaces it for
# that scene. The same two captions must take longer under a 2 s hold than under `caption-min 0`.
#
#   cmake -DRECORDER=<cyd_demo_recorder> -DHELD=<scene.scn> -DFREE=<scene.scn> -P demo_recorder_caption_min.cmake
function(frames_of result scene)
  execute_process(COMMAND "${RECORDER}" "${scene}" --min-caption=2000 RESULT_VARIABLE code ERROR_VARIABLE log OUTPUT_QUIET)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "the recorder failed with ${code} for ${scene}:\n${log}")
  endif()
  string(REGEX MATCH "([0-9]+) frames" matched "${log}")
  set(${result} ${CMAKE_MATCH_1} PARENT_SCOPE)
endfunction()

frames_of(held "${HELD}")
frames_of(free "${FREE}")
if(NOT held GREATER free)
  message(FATAL_ERROR "a 2 s hold should lengthen the scene: ${held} frames against ${free} with caption-min 0")
endif()
math(EXPR gap "${held} - ${free}")
if(gap LESS 100)
  message(FATAL_ERROR "the hold added only ${gap} frames; two seconds is 120")
endif()
message(STATUS "held ${held} frames, free ${free}")
