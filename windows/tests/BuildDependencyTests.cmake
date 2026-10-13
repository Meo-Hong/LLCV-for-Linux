cmake_minimum_required(VERSION 3.24)
# This isolated fixture needs neither a Windows SDK environment nor the linker.
# Test the actual Ninja scanner, not merely a string in the generated rules.
file(MAKE_DIRECTORY "${TEST_BINARY_DIR}/source")
file(WRITE "${TEST_BINARY_DIR}/source/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.24)\nproject(DependencyProbe LANGUAGES CXX)\ninclude(\"${SOURCE_DIR}/cmake/MsvcDependencies.cmake\")\nadd_library(probe OBJECT probe.cpp)\n")
file(WRITE "${TEST_BINARY_DIR}/source/probe.cpp"
    "#include \"value.h\"\nint dependency_probe() { return VALUE; }\n")
file(WRITE "${TEST_BINARY_DIR}/source/value.h" "#define VALUE 17\n")
function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed (${result}): ${ARGV}\n${output}\n${error}")
    endif()
endfunction()
run_checked("${CMAKE_COMMAND}" -S "${TEST_BINARY_DIR}/source" -B "${TEST_BINARY_DIR}/build"
    -G Ninja "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}" "-DCMAKE_CXX_COMPILER=${COMPILER}"
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_WORKS=TRUE -DCMAKE_CXX_ABI_COMPILED=TRUE)
run_checked("${CMAKE_COMMAND}" --build "${TEST_BINARY_DIR}/build")
set(object "${TEST_BINARY_DIR}/build/CMakeFiles/probe.dir/probe.cpp.obj")
file(SHA256 "${object}" before)
execute_process(COMMAND "${MAKE_PROGRAM}" -C "${TEST_BINARY_DIR}/build" -t deps
    OUTPUT_VARIABLE dependencies COMMAND_ERROR_IS_FATAL ANY)
if(NOT dependencies MATCHES "value.h")
    message(FATAL_ERROR "The compiler's header dependencies were not recorded: ${dependencies}")
endif()
file(WRITE "${TEST_BINARY_DIR}/source/value.h" "#define VALUE 29\n")
run_checked("${CMAKE_COMMAND}" --build "${TEST_BINARY_DIR}/build")
file(SHA256 "${object}" after)
if(before STREQUAL after)
    message(FATAL_ERROR "A header-only edit did not change the compiled object")
endif()
message(STATUS "Localized MSVC/Ninja header tracking and header-only rebuild passed")
