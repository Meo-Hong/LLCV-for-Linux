cmake_minimum_required(VERSION 3.24)

# Preprocess the real application translation unit using the product target's
# definitions/include paths. This neither links nor launches the app, opens a
# capture device, nor depends on optimizer dead-code elimination.
foreach(required SOURCE_DIR COMPILER ARGUMENTS_FILE TEST_BINARY_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
if(NOT EXISTS "${SOURCE_DIR}/src/main.cpp" OR NOT EXISTS "${ARGUMENTS_FILE}")
    message(FATAL_ERROR "Application source or generated target arguments are missing")
endif()

file(READ "${ARGUMENTS_FILE}" target_arguments)
# The diagnostic configuration runs this same test. Normalize that one option
# before testing both branches; leave all other product-target definitions intact.
string(REPLACE "\r\n" "\n" target_arguments "${target_arguments}")
string(REPLACE "\n" ";" argument_lines "${target_arguments}")
list(FILTER argument_lines EXCLUDE REGEX "LLCV_AUDIO_INPUT_RECOVERY_DIAGNOSTIC")
list(JOIN argument_lines "\n" common_arguments)
file(MAKE_DIRECTORY "${TEST_BINARY_DIR}")

set(diagnostic_symbols AudioOnlyCaptureAttempt g_audioInputRecoveryExhausted
    AudioInputWatchdog RunAudioInputRecovery)
foreach(variant normal diagnostic)
    set(arguments "${TEST_BINARY_DIR}/${variant}.rsp")
    set(preprocessed "${TEST_BINARY_DIR}/${variant}.i")
    set(definition "")
    if(variant STREQUAL "diagnostic")
        set(definition "/DLLCV_AUDIO_INPUT_RECOVERY_DIAGNOSTIC\n")
    endif()
    file(WRITE "${arguments}" "${common_arguments}\n${definition}")
    execute_process(
        COMMAND "${COMPILER}" "@${arguments}" /P "/Fi${preprocessed}"
            "${SOURCE_DIR}/src/main.cpp"
        WORKING_DIRECTORY "${TEST_BINARY_DIR}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors
        TIMEOUT 60)
    if(NOT result STREQUAL "0" OR NOT EXISTS "${preprocessed}")
        message(FATAL_ERROR "${variant} preprocessing failed (${result}):\n${output}\n${errors}")
    endif()
    file(READ "${preprocessed}" source)
    if(NOT source MATCHES "static[ \t\r\n]+bool[ \t\r\n]+AudioOnlyCaptureLoop[ \t\r\n]*\\(")
        message(FATAL_ERROR "${variant}: the real audio-only capture entry point was not processed")
    endif()
    foreach(symbol IN LISTS diagnostic_symbols)
        string(FIND "${source}" "${symbol}" found)
        if(variant STREQUAL "normal" AND NOT found EQUAL -1)
            message(FATAL_ERROR "Release preprocessing still contains diagnostic code: ${symbol}")
        elseif(variant STREQUAL "diagnostic" AND found EQUAL -1)
            message(FATAL_ERROR "Diagnostic preprocessing lost its opt-in code: ${symbol}")
        endif()
    endforeach()
endforeach()

# Remove only these exact test-owned intermediates; retain failures for inspection.
file(REMOVE "${TEST_BINARY_DIR}/normal.rsp" "${TEST_BINARY_DIR}/normal.i"
    "${TEST_BINARY_DIR}/diagnostic.rsp" "${TEST_BINARY_DIR}/diagnostic.i")
message(STATUS "Audio-input recovery is absent from normal preprocessing and present only in the diagnostic branch")
