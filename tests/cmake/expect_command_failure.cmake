if(NOT DEFINED EXECUTABLE)
    message(FATAL_ERROR "EXECUTABLE is required")
endif()
if(NOT DEFINED EXPECTED_ERROR)
    message(FATAL_ERROR "EXPECTED_ERROR is required")
endif()
if(NOT DEFINED ARGUMENT_COUNT OR NOT ARGUMENT_COUNT MATCHES "^[0-8]$")
    message(FATAL_ERROR "ARGUMENT_COUNT from 0 through 8 is required")
endif()

set(COMMAND_ARGUMENTS)
foreach(ARGUMENT_INDEX RANGE 1 9)
    set(ARGUMENT_NAME "ARGUMENT_${ARGUMENT_INDEX}")
    if(ARGUMENT_INDEX LESS_EQUAL ARGUMENT_COUNT)
        if(NOT DEFINED ${ARGUMENT_NAME})
            message(FATAL_ERROR "${ARGUMENT_NAME} is required by ARGUMENT_COUNT")
        endif()
        list(APPEND COMMAND_ARGUMENTS "${${ARGUMENT_NAME}}")
    elseif(DEFINED ${ARGUMENT_NAME})
        message(FATAL_ERROR "${ARGUMENT_NAME} exceeds ARGUMENT_COUNT or the eight-argument limit")
    endif()
endforeach()

execute_process(
    COMMAND "${EXECUTABLE}" ${COMMAND_ARGUMENTS}
    RESULT_VARIABLE COMMAND_RESULT
    OUTPUT_VARIABLE COMMAND_OUTPUT
    ERROR_VARIABLE COMMAND_ERROR
)

if(COMMAND_RESULT EQUAL 0)
    message(FATAL_ERROR "Command unexpectedly succeeded")
endif()

string(CONCAT COMPLETE_OUTPUT "${COMMAND_OUTPUT}" "${COMMAND_ERROR}")
string(FIND "${COMPLETE_OUTPUT}" "${EXPECTED_ERROR}" ERROR_OFFSET)
if(ERROR_OFFSET EQUAL -1)
    message(FATAL_ERROR
        "Command failed without the expected diagnostic:\n${COMPLETE_OUTPUT}"
    )
endif()

if(DEFINED EXPECTED_ERROR_2)
    string(FIND "${COMPLETE_OUTPUT}" "${EXPECTED_ERROR_2}" ERROR_OFFSET_2)
    if(ERROR_OFFSET_2 EQUAL -1)
        message(FATAL_ERROR
            "Command failed without the second expected diagnostic:\n${COMPLETE_OUTPUT}"
        )
    endif()
endif()
