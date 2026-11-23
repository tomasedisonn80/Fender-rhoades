# Match the intentional validation error code, rather than accepting any failure
# (a sanitizer crash or an unavailable executable must not make this test pass).
if(DEFINED ARG2)
    execute_process(COMMAND "${PROGRAM}" "${ARG1}" "${ARG2}"
        RESULT_VARIABLE result ERROR_VARIABLE diagnostic)
else()
    execute_process(COMMAND "${PROGRAM}" "${ARG1}"
        RESULT_VARIABLE result ERROR_VARIABLE diagnostic)
endif()
if(NOT "${result}" STREQUAL "2" OR NOT diagnostic MATCHES "rhodes_render:")
    message(FATAL_ERROR "Expected validation exit code 2; received ${result}: ${diagnostic}")
endif()
