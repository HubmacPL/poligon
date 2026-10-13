# Runs PROGRAM with ARGUMENTS twice and fails unless both runs succeed and print identical output.
# Usage: cmake -DPROGRAM=<exe> "-DARGUMENTS=<space separated arguments>" -P CompareTwoRuns.cmake
if(NOT DEFINED PROGRAM)
  message(FATAL_ERROR "PROGRAM is not set")
endif()
separate_arguments(argument_list NATIVE_COMMAND "${ARGUMENTS}")

foreach(run first second)
  execute_process(
    COMMAND "${PROGRAM}" ${argument_list}
    RESULT_VARIABLE ${run}_result
    OUTPUT_VARIABLE ${run}_output
    ERROR_VARIABLE ${run}_error)
  if(NOT ${run}_result EQUAL 0)
    message(FATAL_ERROR "${run} run failed with exit code ${${run}_result}\n${${run}_output}\n${${run}_error}")
  endif()
endforeach()

if(first_output STREQUAL "")
  message(FATAL_ERROR "The program printed nothing, so there is nothing to compare")
endif()

if(NOT first_output STREQUAL second_output)
  message(FATAL_ERROR "Outputs differ.\n--- first run ---\n${first_output}\n--- second run ---\n${second_output}")
endif()

message(STATUS "Both runs printed identical output:\n${first_output}")
