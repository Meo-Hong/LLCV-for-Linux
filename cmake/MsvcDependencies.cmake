# Older CMake versions may decode MSVC's localized /showIncludes output as
# UTF-8 although the compiler uses the Windows ANSI code page. Detect with the
# correct encoding so Ninja receives the same prefix as the compiler output.
# No extra language pack or global console/locale change is needed.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(llcv_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/llcv-dependencies")
    file(MAKE_DIRECTORY "${llcv_probe_dir}")
    file(WRITE "${llcv_probe_dir}/llcv-probe.h" "\n")
    file(WRITE "${llcv_probe_dir}/probe.cpp" "#include \"llcv-probe.h\"\n")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /EP
        "${llcv_probe_dir}/probe.cpp" ENCODING ANSI
        OUTPUT_VARIABLE llcv_probe_out ERROR_VARIABLE llcv_probe_err
        RESULT_VARIABLE llcv_probe_result TIMEOUT 15)
    file(TO_NATIVE_PATH "${llcv_probe_dir}/llcv-probe.h" llcv_probe_header)
    string(REPLACE "\r\n" "\n" llcv_probe_lines "${llcv_probe_out}\n${llcv_probe_err}")
    string(REPLACE "\n" ";" llcv_probe_lines "${llcv_probe_lines}")
    set(llcv_found_prefix FALSE)
    foreach(llcv_line IN LISTS llcv_probe_lines)
        string(FIND "${llcv_line}" "${llcv_probe_header}" llcv_path_offset)
        if(llcv_path_offset GREATER 0)
            string(SUBSTRING "${llcv_line}" 0 ${llcv_path_offset} CMAKE_CL_SHOWINCLUDES_PREFIX)
            set(llcv_found_prefix TRUE)
            break()
        endif()
    endforeach()
    if(NOT llcv_probe_result EQUAL 0 OR NOT llcv_found_prefix)
        message(FATAL_ERROR "Could not verify MSVC header dependency output; refusing an unsafe incremental build")
    endif()
endif()
