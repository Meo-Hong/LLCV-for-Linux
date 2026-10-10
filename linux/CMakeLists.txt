cmake_minimum_required(VERSION 3.22...3.25)
project(llcv VERSION 2.0.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

set(LLCV_APP_ID "io.github.seria_aa.LLCV")
option(LLCV_SOURCE_TREE_DATA "Also look for fonts and icons in the source tree (development builds)" ON)
option(LLCV_USE_SYSTEM_IMGUI "Use the system Dear ImGui 1.92 or newer instead of the bundled copy" OFF)
set(LLCV_WAYLAND_COLOR_MANAGEMENT AUTO CACHE STRING "Wayland HDR output through wp_color_management_v1 (AUTO, ON, OFF)")
set_property(CACHE LLCV_WAYLAND_COLOR_MANAGEMENT PROPERTY STRINGS AUTO ON OFF)
set(LLCV_SDL3 AUTO CACHE STRING "SDL3 source: AUTO (system, else bundled), SYSTEM, BUNDLED")
set_property(CACHE LLCV_SDL3 PROPERTY STRINGS AUTO SYSTEM BUNDLED)
set(LLCV_SDL3_BUNDLED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3/SDL3-3.4.18)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

if (NOT LLCV_SDL3 STREQUAL "BUNDLED")
    find_package(SDL3 3.2 QUIET CONFIG COMPONENTS SDL3)
endif()
if (SDL3_FOUND AND NOT LLCV_SDL3 STREQUAL "BUNDLED")
    set(LLCV_SDL_TARGET SDL3::SDL3)
    message(STATUS "SDL3: system ${SDL3_VERSION}")
elseif (LLCV_SDL3 STREQUAL "SYSTEM")
    message(FATAL_ERROR "SDL3 3.2 or newer was not found. Install it, or configure with -DLLCV_SDL3=BUNDLED.")
else()
    if (NOT EXISTS ${LLCV_SDL3_BUNDLED_DIR}/CMakeLists.txt)
        message(FATAL_ERROR "SDL3 3.2 or newer is not installed. Run tools/fetch-sdl3.sh to download the pinned SDL3 source.")
    endif()
    foreach(option SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL SDL_CAMERA SDL_JOYSTICK
                   SDL_HAPTIC SDL_HIDAPI SDL_SENSOR SDL_GPU SDL_RENDER SDL_DIALOG SDL_TRAY SDL_KMSDRM
                   SDL_VULKAN SDL_JACK SDL_SNDIO SDL_OSS SDL_LIBUDEV)
        set(${option} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    pkg_check_modules(LLCV_XSCRNSAVER QUIET xscrnsaver)
    if (NOT LLCV_XSCRNSAVER_FOUND)
        set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
    endif()
    if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
        add_subdirectory(${LLCV_SDL3_BUNDLED_DIR} ${CMAKE_CURRENT_BINARY_DIR}/sdl3 EXCLUDE_FROM_ALL SYSTEM)
    else()
        add_subdirectory(${LLCV_SDL3_BUNDLED_DIR} ${CMAKE_CURRENT_BINARY_DIR}/sdl3 EXCLUDE_FROM_ALL)
    endif()
    set(LLCV_SDL_TARGET SDL3::SDL3-static)
    message(STATUS "SDL3: bundled 3.4.18 (static)")
endif()
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)

if (LLCV_USE_SYSTEM_IMGUI)
    pkg_check_modules(IMGUI REQUIRED IMPORTED_TARGET imgui>=1.92)
    add_library(llcv_imgui INTERFACE)
    target_link_libraries(llcv_imgui INTERFACE PkgConfig::IMGUI)
else()
    set(LLCV_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
    add_library(llcv_imgui STATIC
        ${LLCV_IMGUI_DIR}/imgui.cpp
        ${LLCV_IMGUI_DIR}/imgui_demo.cpp
        ${LLCV_IMGUI_DIR}/imgui_draw.cpp
        ${LLCV_IMGUI_DIR}/imgui_tables.cpp
        ${LLCV_IMGUI_DIR}/imgui_widgets.cpp
        ${LLCV_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(llcv_imgui SYSTEM PUBLIC ${LLCV_IMGUI_DIR})
    target_link_libraries(llcv_imgui PUBLIC ${CMAKE_DL_LIBS})
endif()

set(LLCV_HAVE_COLOR_MANAGEMENT OFF)
if (NOT LLCV_WAYLAND_COLOR_MANAGEMENT STREQUAL "OFF")
    pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
    pkg_check_modules(WAYLAND_PROTOCOLS wayland-protocols>=1.41)
    pkg_check_modules(WAYLAND_SCANNER wayland-scanner)
    set(LLCV_COLOR_MANAGEMENT_XML ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wayland-protocols/color-management-v1.xml)
    if (WAYLAND_PROTOCOLS_FOUND)
        pkg_get_variable(LLCV_WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
        if (EXISTS ${LLCV_WAYLAND_PROTOCOLS_DIR}/staging/color-management/color-management-v1.xml)
            set(LLCV_COLOR_MANAGEMENT_XML
                ${LLCV_WAYLAND_PROTOCOLS_DIR}/staging/color-management/color-management-v1.xml)
        endif()
    endif()
    if (WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER_FOUND)
        pkg_get_variable(LLCV_WAYLAND_SCANNER wayland-scanner wayland_scanner)
        if (EXISTS ${LLCV_COLOR_MANAGEMENT_XML} AND LLCV_WAYLAND_SCANNER)
            set(LLCV_HAVE_COLOR_MANAGEMENT ON)
        endif()
    endif()
    if (NOT LLCV_HAVE_COLOR_MANAGEMENT AND LLCV_WAYLAND_COLOR_MANAGEMENT STREQUAL "ON")
        message(FATAL_ERROR "Wayland HDR output needs wayland-client and wayland-scanner")
    endif()
endif()
message(STATUS "Wayland HDR output (wp_color_management_v1): ${LLCV_HAVE_COLOR_MANAGEMENT} ${LLCV_COLOR_MANAGEMENT_XML}")

add_executable(llcv
    src/main.cpp
    src/app/App.cpp
    src/app/ModeSelection.cpp
    src/audio/AudioEngine.cpp
    src/audio/AudioMix.cpp
    src/audio/PcmPipeline.cpp
    src/capture/UvcDescriptors.cpp
    src/capture/V4l2Capture.cpp
    src/capture/V4l2Devices.cpp
    src/capture/VideoFrame.cpp
    src/diagnostics/Logger.cpp
    src/platform/Paths.cpp
    src/platform/PngImage.cpp
    src/platform/Strings.cpp
    src/screenshot/Screenshot.cpp
    src/settings/SettingsStore.cpp
    src/ui/ImGuiPlatform.cpp
    src/ui/SettingsScreen.cpp
    src/ui/Text.cpp
    src/ui/Theme.cpp
    src/ui/ViewerOverlay.cpp
    src/video/GlApi.cpp
    src/video/HdrOutput.cpp
    src/video/MjpegDecoder.cpp
    src/video/VideoColor.cpp
    src/video/VideoRenderer.cpp
)

target_include_directories(llcv PRIVATE src)

target_compile_definitions(llcv PRIVATE
    LLCV_VERSION="${PROJECT_VERSION}"
    LLCV_APP_ID="${LLCV_APP_ID}"
    LLCV_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/llcv"
    LLCV_ICON_DIR="${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor"
)

if (LLCV_SOURCE_TREE_DATA)
    target_compile_definitions(llcv PRIVATE LLCV_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if (LLCV_HAVE_COLOR_MANAGEMENT)
    set(LLCV_WAYLAND_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/wayland)
    file(MAKE_DIRECTORY ${LLCV_WAYLAND_GENERATED})
    add_custom_command(
        OUTPUT ${LLCV_WAYLAND_GENERATED}/color-management-v1-client-protocol.h
        COMMAND ${LLCV_WAYLAND_SCANNER} client-header ${LLCV_COLOR_MANAGEMENT_XML}
                ${LLCV_WAYLAND_GENERATED}/color-management-v1-client-protocol.h
        DEPENDS ${LLCV_COLOR_MANAGEMENT_XML}
        VERBATIM)
    add_custom_command(
        OUTPUT ${LLCV_WAYLAND_GENERATED}/color-management-v1-protocol.c
        COMMAND ${LLCV_WAYLAND_SCANNER} private-code ${LLCV_COLOR_MANAGEMENT_XML}
                ${LLCV_WAYLAND_GENERATED}/color-management-v1-protocol.c
        DEPENDS ${LLCV_COLOR_MANAGEMENT_XML}
        VERBATIM)
    target_sources(llcv PRIVATE
        ${LLCV_WAYLAND_GENERATED}/color-management-v1-client-protocol.h
        ${LLCV_WAYLAND_GENERATED}/color-management-v1-protocol.c)
    target_include_directories(llcv PRIVATE ${LLCV_WAYLAND_GENERATED})
    target_compile_definitions(llcv PRIVATE LLCV_HAVE_COLOR_MANAGEMENT=1)
    target_link_libraries(llcv PRIVATE PkgConfig::WAYLAND_CLIENT)
endif()

target_compile_options(llcv PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)

target_link_libraries(llcv PRIVATE
    ${LLCV_SDL_TARGET}
    llcv_imgui
    PkgConfig::TURBOJPEG
    PkgConfig::PNG
    Threads::Threads
)

install(TARGETS llcv RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES
    third_party/pretendard/Pretendard-Regular.ttf
    third_party/pretendard/Pretendard-SemiBold.ttf
    DESTINATION ${CMAKE_INSTALL_DATADIR}/llcv/fonts)

install(FILES data/${LLCV_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES data/${LLCV_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

install(FILES data/llcv.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

foreach(size 16 24 32 48 64 128 256)
    install(FILES data/icons/${size}x${size}/${LLCV_APP_ID}.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
endforeach()
