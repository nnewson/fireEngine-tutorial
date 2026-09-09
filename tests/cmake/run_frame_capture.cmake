if(NOT DEFINED EXECUTABLE OR NOT DEFINED OUTPUT_PATH)
    message(FATAL_ERROR "EXECUTABLE and OUTPUT_PATH are required")
endif()

get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(REMOVE "${OUTPUT_PATH}")

execute_process(
    # Put capture controls before their bounded mode to pin B1's
    # order-independent grammar on the complete renderer integration path.
    COMMAND "${EXECUTABLE}" --capture "${OUTPUT_PATH}" --capture-frame 3 --smoke basic
    RESULT_VARIABLE COMMAND_RESULT
    OUTPUT_VARIABLE COMMAND_OUTPUT
    ERROR_VARIABLE COMMAND_ERROR
)
string(CONCAT COMPLETE_OUTPUT "${COMMAND_OUTPUT}" "${COMMAND_ERROR}")

if(NOT COMMAND_RESULT EQUAL 0)
    message(FATAL_ERROR "Capture command failed:\n${COMPLETE_OUTPUT}")
endif()
if(NOT COMPLETE_OUTPUT MATCHES
   "Captured frame 3 to [^\n]* \\(([0-9]+)x([0-9]+), [^,]+, [0-9]+ presentation recreations\\)\\.")
    message(FATAL_ERROR "Capture success report is missing or malformed:\n${COMPLETE_OUTPUT}")
endif()
set(REPORTED_WIDTH "${CMAKE_MATCH_1}")
set(REPORTED_HEIGHT "${CMAKE_MATCH_2}")

if(NOT EXISTS "${OUTPUT_PATH}")
    message(FATAL_ERROR "Capture command reported success without creating the PNG")
endif()
file(SIZE "${OUTPUT_PATH}" PNG_SIZE)
if(PNG_SIZE EQUAL 0)
    message(FATAL_ERROR "Capture command created an empty PNG")
endif()

# PNG stores IHDR width and height as big-endian 32-bit integers at byte
# offsets 16 and 20. Read only the fixed header needed to cross-check the
# dimensions printed by the writer.
file(READ "${OUTPUT_PATH}" PNG_HEADER LIMIT 24 HEX)
string(TOLOWER "${PNG_HEADER}" PNG_HEADER)
string(SUBSTRING "${PNG_HEADER}" 0 32 PNG_PREFIX)
if(NOT PNG_PREFIX STREQUAL "89504e470d0a1a0a0000000d49484452")
    message(FATAL_ERROR "Capture output does not contain a valid PNG IHDR prefix")
endif()
string(SUBSTRING "${PNG_HEADER}" 32 8 PNG_WIDTH_HEX)
string(SUBSTRING "${PNG_HEADER}" 40 8 PNG_HEIGHT_HEX)
math(EXPR PNG_WIDTH "0x${PNG_WIDTH_HEX}")
math(EXPR PNG_HEIGHT "0x${PNG_HEIGHT_HEX}")
if(NOT PNG_WIDTH EQUAL REPORTED_WIDTH OR NOT PNG_HEIGHT EQUAL REPORTED_HEIGHT)
    message(FATAL_ERROR
        "PNG dimensions ${PNG_WIDTH}x${PNG_HEIGHT} do not match the report "
        "${REPORTED_WIDTH}x${REPORTED_HEIGHT}"
    )
endif()
