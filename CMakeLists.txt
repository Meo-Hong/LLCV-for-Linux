cmake_minimum_required(VERSION 3.24)
project(LowLatencyCaptureViewer VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WIN32_EXECUTABLE ON)
add_compile_definitions(LLCV_VSR_FEATURE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(LowLatencyCaptureViewer
    src/main.cpp
    src/app.rc
    src/audio/AsioOutput.cpp
    src/audio/AudioMix.cpp
    src/audio/AudioDeviceCapabilities.cpp
    src/audio/WasapiOutput.cpp
    src/audio/CaptureAudioFormat.cpp
    src/audio/PcmPipeline.cpp
    src/capture/DirectShowDevices.cpp
    src/capture/AudioSampleGrabber.cpp
    src/capture/DirectShowGraphResources.cpp
    src/capture/LatestVideoSample.cpp
    src/capture/HardwareToneMapping.cpp
    src/capture/ElgatoHidToneMapping.cpp
    src/diagnostics/Logger.cpp
    src/diagnostics/AudioErrorHistory.cpp
    src/settings/SettingsStore.cpp
    src/screenshot/ScreenshotPixels.cpp
    src/screenshot/ScreenshotService.cpp
    src/ui/AudioOsdLayout.cpp
    src/ui/AudioOnlyView.cpp
    src/ui/PresentationModeUi.cpp
    src/ui/SettingsView.cpp
    src/ui/SettingsTheme.cpp
    src/ui/SettingsDialogControls.cpp
    src/ui/UiText.cpp
    src/ui/ViewerHelpWindow.cpp
    src/ui/WindowGeometry.cpp
    src/update/UpdateChecker.cpp
    src/video/DirectShowVideoFormat.cpp
    src/video/MjpegDecoder.cpp
    src/video/VideoColor.cpp
    src/video/HdrDisplay.cpp
    src/video/VsrExperiment.cpp
)

target_include_directories(LowLatencyCaptureViewer PRIVATE
    src
    third_party/asio/common
    third_party/asio/host
    third_party/asio/host/pc
)

# Keep help layout ABI and shared shortcut text current with localized MSVC scans.
set_property(SOURCE src/main.cpp src/ui/AudioOnlyView.cpp src/ui/ViewerHelpWindow.cpp
    src/ui/SettingsTheme.cpp tests/ViewerHelpTests.cpp tests/AudioOnlyViewTests.cpp
    APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/AppPalette.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/AudioOnlyView.h")
set_property(SOURCE src/main.cpp src/ui/ViewerHelpWindow.cpp src/ui/SettingsDialogControls.cpp
    tests/SettingsViewTests.cpp tests/ViewerHelpTests.cpp
    APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ViewerHelpWindow.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ViewerShortcuts.h")
set_property(SOURCE src/main.cpp src/ui/SettingsView.cpp src/ui/SettingsTheme.cpp
    src/ui/SettingsDialogControls.cpp tests/SettingsViewTests.cpp tests/SettingsThemeTests.cpp
    tests/AudioCallbackTests.cpp tests/HdrPipelineTests.cpp tests/MonitorMoveTests.cpp
    tests/SharedReplayTests.cpp tests/VsrExperimentTests.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SettingsView.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SettingsTheme.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SettingsFonts.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/OverlayStyle.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SettingsModeCache.h")
set_property(SOURCE tests/SettingsModeCacheTests.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SettingsModeCache.h")
set_property(SOURCE tests/VsrExperimentTests.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/VsrTimingValidation.inl")

option(LLCV_HDR_FRAME_AUDIT "Build a private F8 single-frame HDR diagnostic (not for release)" OFF)
option(LLCV_HDR_SCRGB_PROTOTYPE "Private direct P010 shader/scRGB comparison (not for release)" OFF)
option(LLCV_VSR_EXPERIMENT "Private NVIDIA VSR A/B experiment (not for release)" OFF)
if (LLCV_VSR_EXPERIMENT)
    target_compile_definitions(LowLatencyCaptureViewer PRIVATE LLCV_VSR_EXPERIMENT)
    set_property(SOURCE src/main.cpp APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/video/VsrExperiment.h")
    set_target_properties(LowLatencyCaptureViewer PROPERTIES OUTPUT_NAME "LowLatencyCaptureViewer_VSR_Test")
    if (LLCV_HDR_SCRGB_PROTOTYPE OR LLCV_HDR_FRAME_AUDIT)
        message(FATAL_ERROR "Build VSR and HDR experiments separately for unambiguous measurements")
    endif()
endif()
if (LLCV_HDR_SCRGB_PROTOTYPE)
    set(LLCV_HDR_FRAME_AUDIT ON)
    target_compile_definitions(LowLatencyCaptureViewer PRIVATE LLCV_HDR_SCRGB_PROTOTYPE)
endif()
if (LLCV_HDR_FRAME_AUDIT)
    target_compile_definitions(LowLatencyCaptureViewer PRIVATE LLCV_HDR_FRAME_AUDIT)
    set_target_properties(LowLatencyCaptureViewer PROPERTIES OUTPUT_NAME "LowLatencyCaptureViewer_HDR_Audit")
endif()
if (LLCV_HDR_SCRGB_PROTOTYPE)
    set_target_properties(LowLatencyCaptureViewer PROPERTIES OUTPUT_NAME "LowLatencyCaptureViewer_HDR_Compare")
endif()

# Steinberg's host-side ASIO helper is compiled into the optional backend.
# Driver DLLs are not bundled; installed ASIO drivers are discovered at runtime.
target_sources(LowLatencyCaptureViewer PRIVATE
    third_party/asio/common/asio.cpp
    third_party/asio/host/asiodrivers.cpp
    third_party/asio/host/pc/asiolist.cpp
)
set_source_files_properties(
    third_party/asio/common/asio.cpp
    third_party/asio/host/asiodrivers.cpp
    third_party/asio/host/pc/asiolist.cpp
    PROPERTIES COMPILE_OPTIONS "/UUNICODE;/U_UNICODE"
)

target_compile_definitions(LowLatencyCaptureViewer PRIVATE
    UNICODE
    _UNICODE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(LowLatencyCaptureViewer PRIVATE
    ole32
    oleaut32
    strmiids
    uuid
    avrt
    d3d11
    dxgi
    d2d1
    dwrite
    dwmapi
    d3dcompiler
    shcore
    comctl32
    mfplat
    mfuuid
    advapi32
    winhttp
    hid
    setupapi
    windowscodecs
    shell32
)

if (MSVC)
    target_compile_options(LowLatencyCaptureViewer PRIVATE /W4 /EHsc /permissive- /utf-8)
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(ViewerHelpTests tests/ViewerHelpTests.cpp src/ui/ViewerHelpWindow.cpp)
    set_property(SOURCE tests/ViewerHelpTests.cpp APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ViewerHelpWindow.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ViewerShortcuts.h")
    target_include_directories(ViewerHelpTests PRIVATE src)
    target_compile_definitions(ViewerHelpTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_link_libraries(ViewerHelpTests PRIVATE user32 gdi32 dwmapi gdiplus imm32)
    set_target_properties(ViewerHelpTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(ViewerHelpTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME ViewerHelpTests COMMAND ViewerHelpTests)
    add_test(NAME ViewerHelpImeTests COMMAND ViewerHelpTests --with-ime)
    set_tests_properties(ViewerHelpTests ViewerHelpImeTests PROPERTIES TIMEOUT 45)
    add_executable(ScreenshotTests tests/ScreenshotTests.cpp tests/HdrScreenshotReference.cpp
        src/screenshot/ScreenshotPixels.cpp src/screenshot/ScreenshotService.cpp)
    target_include_directories(ScreenshotTests PRIVATE src)
    target_compile_definitions(ScreenshotTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(ScreenshotTests PRIVATE ole32 oleaut32 windowscodecs shell32 user32 gdi32 uuid)
    set_target_properties(ScreenshotTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(ScreenshotTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME ScreenshotTests COMMAND ScreenshotTests)
    set_tests_properties(ScreenshotTests PROPERTIES TIMEOUT 60)
    add_executable(MjpegDecoderTests tests/MjpegDecoderTests.cpp src/video/VideoColor.cpp)
    target_include_directories(MjpegDecoderTests PRIVATE src)
    target_compile_definitions(MjpegDecoderTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(MjpegDecoderTests PRIVATE mfplat mfuuid ole32 strmiids windowscodecs)
    set_target_properties(MjpegDecoderTests PROPERTIES WIN32_EXECUTABLE FALSE)
    set_property(SOURCE tests/MjpegDecoderTests.cpp APPEND PROPERTY
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/video/MjpegDecoder.cpp")
    if (MSVC)
        target_compile_options(MjpegDecoderTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME MjpegDecoderTests COMMAND MjpegDecoderTests)
    add_test(NAME MjpegNativeDecoderTests COMMAND MjpegDecoderTests --native)
    set_tests_properties(MjpegNativeDecoderTests PROPERTIES TIMEOUT 30)
    add_executable(WasapiInitializationTests tests/WasapiInitializationTests.cpp
        src/audio/AudioDeviceCapabilities.cpp)
    target_include_directories(WasapiInitializationTests PRIVATE src)
    target_compile_definitions(WasapiInitializationTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(WasapiInitializationTests PRIVATE ole32 oleaut32 uuid avrt)
    set_target_properties(WasapiInitializationTests PROPERTIES WIN32_EXECUTABLE FALSE)
    set_property(SOURCE tests/WasapiInitializationTests.cpp APPEND PROPERTY
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/WasapiOutput.cpp")
    set_property(SOURCE tests/AudioCallbackTests.cpp APPEND PROPERTY
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/AsioOutput.cpp")
    if (MSVC)
        target_compile_options(WasapiInitializationTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME WasapiInitializationTests COMMAND WasapiInitializationTests)
    add_executable(WasapiRenderTests tests/WasapiRenderTests.cpp
        src/audio/AudioDeviceCapabilities.cpp)
    target_include_directories(WasapiRenderTests PRIVATE src)
    target_compile_definitions(WasapiRenderTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(WasapiRenderTests PRIVATE ole32 oleaut32 uuid avrt)
    set_target_properties(WasapiRenderTests PROPERTIES WIN32_EXECUTABLE FALSE)
    set_property(SOURCE tests/WasapiRenderTests.cpp APPEND PROPERTY
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/WasapiOutput.cpp")
    if (MSVC)
        target_compile_options(WasapiRenderTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME WasapiRenderTests COMMAND WasapiRenderTests)
    set_tests_properties(WasapiRenderTests MjpegDecoderTests PROPERTIES TIMEOUT 30)
    add_executable(SurroundAudioTests tests/SurroundAudioTests.cpp
        src/capture/AudioSampleGrabber.cpp
        src/audio/CaptureAudioFormat.cpp src/audio/PcmPipeline.cpp src/audio/AudioMix.cpp)
    target_include_directories(SurroundAudioTests PRIVATE src)
    target_compile_definitions(SurroundAudioTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(SurroundAudioTests PRIVATE strmiids ole32)
    set_target_properties(SurroundAudioTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(SurroundAudioTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME SurroundAudioTests COMMAND SurroundAudioTests)
    # Fake COM/HID only: never open capture devices in regression tests.
    foreach(tonemap_test HardwareToneMappingTests VendorUsbToneMappingTests)
        add_executable(${tonemap_test} tests/${tonemap_test}.cpp
            src/capture/HardwareToneMapping.cpp src/capture/ElgatoHidToneMapping.cpp)
        target_include_directories(${tonemap_test} PRIVATE src)
        target_compile_definitions(${tonemap_test} PRIVATE
            UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
        target_link_libraries(${tonemap_test} PRIVATE strmiids uuid hid setupapi)
        set_target_properties(${tonemap_test} PROPERTIES WIN32_EXECUTABLE FALSE)
        if (MSVC)
            target_compile_options(${tonemap_test} PRIVATE /W4 /EHsc /permissive- /utf-8)
        endif()
        add_test(NAME ${tonemap_test} COMMAND ${tonemap_test})
    endforeach()
    add_executable(HdrPolicyTests tests/HdrPolicyTests.cpp)
    target_include_directories(HdrPolicyTests PRIVATE src)
    target_compile_definitions(HdrPolicyTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(HdrPolicyTests PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME HdrPolicyTests COMMAND HdrPolicyTests)
    add_executable(OutputTransitionStateTests tests/OutputTransitionStateTests.cpp)
    target_include_directories(OutputTransitionStateTests PRIVATE src)
    set_target_properties(OutputTransitionStateTests PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME OutputTransitionStateTests COMMAND OutputTransitionStateTests)

    add_executable(SettingsViewTests tests/SettingsViewTests.cpp
        src/ui/SettingsView.cpp src/ui/PresentationModeUi.cpp
        src/ui/SettingsDialogControls.cpp src/ui/UiText.cpp)
    target_include_directories(SettingsViewTests PRIVATE src)
    target_compile_definitions(SettingsViewTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_link_libraries(SettingsViewTests PRIVATE comctl32)
    set_target_properties(SettingsViewTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(OutputTransitionStateTests PRIVATE /W4 /EHsc /permissive- /utf-8)
        target_compile_options(SettingsViewTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME SettingsViewTests COMMAND SettingsViewTests)
    add_executable(SettingsModeCacheTests tests/SettingsModeCacheTests.cpp)
    target_include_directories(SettingsModeCacheTests PRIVATE src)
    set_target_properties(SettingsModeCacheTests PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME SettingsModeCacheTests COMMAND SettingsModeCacheTests)
    set_tests_properties(SettingsModeCacheTests PROPERTIES TIMEOUT 15)
    # Native control creation is substantially slower than pure geometry tests,
    # especially when other replay tests run concurrently on CI.
    set_tests_properties(SettingsViewTests PROPERTIES TIMEOUT 120)
    add_executable(SettingsThemeTests tests/SettingsThemeTests.cpp
        src/ui/SettingsTheme.cpp src/ui/SettingsView.cpp src/ui/PresentationModeUi.cpp
        src/ui/SettingsDialogControls.cpp src/ui/UiText.cpp)
    target_include_directories(SettingsThemeTests PRIVATE src)
    target_compile_definitions(SettingsThemeTests PRIVATE UNICODE _UNICODE NOMINMAX
        WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_link_libraries(SettingsThemeTests PRIVATE comctl32 dwmapi)
    set_target_properties(SettingsThemeTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(SettingsThemeTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME SettingsThemeTests COMMAND SettingsThemeTests)
    # Hidden native fixture clicks can take 2 s each on the test host.
    # Keep all 32 lifecycle checks; allow the
    # measured ~70 s run instead of mistaking that finite work for a deadlock.
    set_tests_properties(SettingsThemeTests PROPERTIES TIMEOUT 120)
    # Local visual/keyboard QA with synthetic devices; never installed and
    # never reads or saves a user's settings or opens capture/audio hardware.
    get_target_property(settings_preview_sources SettingsThemeTests SOURCES)
    add_executable(SettingsPreview WIN32 ${settings_preview_sources})
    target_include_directories(SettingsPreview PRIVATE src)
    target_compile_definitions(SettingsPreview PRIVATE LLCV_SETTINGS_PREVIEW
        $<TARGET_PROPERTY:SettingsThemeTests,COMPILE_DEFINITIONS>)
    target_link_libraries(SettingsPreview PRIVATE comctl32 dwmapi)
    if (MSVC)
        target_compile_options(SettingsPreview PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_executable(LatestVideoSampleTests tests/LatestVideoSampleTests.cpp
        src/capture/LatestVideoSample.cpp
        src/video/DirectShowVideoFormat.cpp
        src/capture/DirectShowGraphResources.cpp)
    target_include_directories(LatestVideoSampleTests PRIVATE src)
    target_compile_definitions(LatestVideoSampleTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(LatestVideoSampleTests PRIVATE ole32 strmiids uuid mfuuid user32)
    set_target_properties(LatestVideoSampleTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(LatestVideoSampleTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME LatestVideoSampleTests COMMAND LatestVideoSampleTests)
    set_tests_properties(LatestVideoSampleTests PROPERTIES TIMEOUT 15)
    # Exercise application callbacks and the ASIO notification protocol without
    # enumerating/opening hardware. This target is never installed.
    # These test translation units include the implementation directly. Keep
    # that dependency explicit even when a localized MSVC include scan misses it.
    set_property(SOURCE tests/AudioCallbackTests.cpp tests/HdrPipelineTests.cpp
        tests/MonitorMoveTests.cpp tests/SharedReplayTests.cpp
        APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ViewerHelpWindow.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ViewerShortcuts.h")
    get_target_property(audio_callback_sources LowLatencyCaptureViewer SOURCES)
    list(FILTER audio_callback_sources EXCLUDE REGEX "src/(main.cpp|app.rc|audio/AsioOutput.cpp)$")
    if (WIN32)
        add_executable(VsrExperimentTests tests/VsrExperimentTests.cpp ${audio_callback_sources})
        target_compile_definitions(VsrExperimentTests PRIVATE LLCV_SETTINGS_MODE_TEST)
        set_property(SOURCE tests/VsrExperimentTests.cpp APPEND PROPERTY OBJECT_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/video/VsrExperiment.h")
        target_include_directories(VsrExperimentTests PRIVATE
            $<TARGET_PROPERTY:LowLatencyCaptureViewer,INCLUDE_DIRECTORIES>)
        target_compile_definitions(VsrExperimentTests PRIVATE
            $<TARGET_PROPERTY:LowLatencyCaptureViewer,COMPILE_DEFINITIONS>)
        target_link_libraries(VsrExperimentTests PRIVATE
            $<TARGET_PROPERTY:LowLatencyCaptureViewer,LINK_LIBRARIES>)
        set_target_properties(VsrExperimentTests PROPERTIES WIN32_EXECUTABLE FALSE)
        if (MSVC)
            target_compile_options(VsrExperimentTests PRIVATE /W4 /EHsc /permissive- /utf-8)
        endif()
        add_test(NAME VsrExperimentTests COMMAND VsrExperimentTests)
        set_tests_properties(VsrExperimentTests PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS GPU)
        add_test(NAME SettingsControllerTests COMMAND VsrExperimentTests --settings-controller)
        set_tests_properties(SettingsControllerTests PROPERTIES TIMEOUT 30)
    endif()
    add_executable(AudioCallbackTests tests/AudioCallbackTests.cpp ${audio_callback_sources})
    target_include_directories(AudioCallbackTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,INCLUDE_DIRECTORIES>)
    target_compile_definitions(AudioCallbackTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,COMPILE_DEFINITIONS>)
    target_link_libraries(AudioCallbackTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,LINK_LIBRARIES>)
    set_target_properties(AudioCallbackTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(AudioCallbackTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME AudioCallbackTests COMMAND AudioCallbackTests)

    add_executable(HdrPipelineTests tests/HdrPipelineTests.cpp ${audio_callback_sources})
    target_include_directories(HdrPipelineTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,INCLUDE_DIRECTORIES>)
    target_compile_definitions(HdrPipelineTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,COMPILE_DEFINITIONS>)
    target_link_libraries(HdrPipelineTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,LINK_LIBRARIES>)
    set_target_properties(HdrPipelineTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(HdrPipelineTests PRIVATE /W4 /EHsc /permissive- /utf-8)
        target_compile_options(HdrPolicyTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME HdrPipelineTests COMMAND HdrPipelineTests)
    add_test(NAME SdrPipelineTests COMMAND HdrPipelineTests --sdr-only)
    add_test(NAME OverlayUiTests COMMAND HdrPipelineTests --osd-ui)
    set_tests_properties(OverlayUiTests PROPERTIES TIMEOUT 60 LABELS GPU)
    set_tests_properties(SdrPipelineTests PROPERTIES TIMEOUT 90 LABELS GPU)
    set_tests_properties(HdrPipelineTests PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS GPU)

    add_executable(MonitorMoveTests tests/MonitorMoveTests.cpp
        ${audio_callback_sources} src/audio/AsioOutput.cpp)
    target_include_directories(MonitorMoveTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,INCLUDE_DIRECTORIES>)
    target_compile_definitions(MonitorMoveTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,COMPILE_DEFINITIONS>)
    target_link_libraries(MonitorMoveTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,LINK_LIBRARIES>)
    set_target_properties(MonitorMoveTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(MonitorMoveTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME MonitorMoveTests COMMAND MonitorMoveTests)
    set_tests_properties(MonitorMoveTests PROPERTIES TIMEOUT 30)

    add_executable(SharedReplayTests tests/SharedReplayTests.cpp
        ${audio_callback_sources} src/audio/AsioOutput.cpp)
    target_include_directories(SharedReplayTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,INCLUDE_DIRECTORIES>)
    target_compile_definitions(SharedReplayTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,COMPILE_DEFINITIONS>)
    target_link_libraries(SharedReplayTests PRIVATE
        $<TARGET_PROPERTY:LowLatencyCaptureViewer,LINK_LIBRARIES>)
    set_target_properties(SharedReplayTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(SharedReplayTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME SharedReplayTests COMMAND SharedReplayTests)
    add_test(NAME SharedSurroundReplay COMMAND SharedReplayTests --surround)
    set_tests_properties(SharedSurroundReplay PROPERTIES TIMEOUT 240)
    set_tests_properties(SharedReplayTests PROPERTIES TIMEOUT 180)
    add_test(NAME SharedReplayExtended COMMAND SharedReplayTests --extended)
    set_tests_properties(SharedReplayExtended PROPERTIES TIMEOUT 240)
    add_test(NAME SharedReplayTarget25 COMMAND SharedReplayTests --target25)
    set_tests_properties(SharedReplayTarget25 PROPERTIES TIMEOUT 240)

    add_executable(AudioModuleTests
        tests/AudioModuleTests.cpp
        src/audio/AudioMix.cpp
        src/audio/CaptureAudioFormat.cpp
        src/audio/PcmPipeline.cpp
        src/ui/AudioOsdLayout.cpp
        src/ui/AudioOnlyView.cpp
    )
    set_target_properties(AudioModuleTests PROPERTIES WIN32_EXECUTABLE FALSE)
    target_include_directories(AudioModuleTests PRIVATE src)
    target_link_libraries(AudioModuleTests PRIVATE strmiids)
    target_compile_definitions(AudioModuleTests PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    if (MSVC)
        target_compile_options(AudioModuleTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME AudioModuleTests COMMAND AudioModuleTests)

    add_executable(AudioOnlyViewTests
        tests/AudioOnlyViewTests.cpp
        src/ui/AudioOnlyView.cpp)
    target_include_directories(AudioOnlyViewTests PRIVATE src)
    set_target_properties(AudioOnlyViewTests PROPERTIES WIN32_EXECUTABLE FALSE)
    target_compile_definitions(AudioOnlyViewTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(AudioOnlyViewTests PRIVATE gdiplus gdi32 user32)
    if (MSVC)
        target_compile_options(AudioOnlyViewTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME AudioOnlyViewTests COMMAND AudioOnlyViewTests)

    add_executable(AudioDeviceCapabilitiesTests
        tests/AudioDeviceCapabilitiesTests.cpp
        src/audio/AudioDeviceCapabilities.cpp
    )
    target_include_directories(AudioDeviceCapabilitiesTests PRIVATE src)
    target_compile_definitions(AudioDeviceCapabilitiesTests PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(AudioDeviceCapabilitiesTests PRIVATE
        ole32
        oleaut32
        uuid
        avrt
    )
    set_target_properties(AudioDeviceCapabilitiesTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(AudioDeviceCapabilitiesTests PRIVATE
            /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME AudioDeviceCapabilitiesTests
             COMMAND AudioDeviceCapabilitiesTests)

    add_executable(LoggerTests
        tests/LoggerTests.cpp
        src/diagnostics/Logger.cpp
    )
    target_include_directories(LoggerTests PRIVATE src)
    target_compile_definitions(LoggerTests PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    set_target_properties(LoggerTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(LoggerTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME LoggerTests COMMAND LoggerTests)

    add_executable(VideoColorTests
        tests/VideoColorTests.cpp
        src/video/VideoColor.cpp
    )
    target_include_directories(VideoColorTests PRIVATE src)
    target_compile_definitions(VideoColorTests PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    set_target_properties(VideoColorTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(VideoColorTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME VideoColorTests COMMAND VideoColorTests)

    add_executable(DirectShowVideoFormatTests
        tests/DirectShowVideoFormatTests.cpp
        src/video/DirectShowVideoFormat.cpp
    )
    target_include_directories(DirectShowVideoFormatTests PRIVATE src)
    target_compile_definitions(DirectShowVideoFormatTests PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(DirectShowVideoFormatTests PRIVATE
        ole32
        strmiids
        mfuuid
    )
    set_target_properties(DirectShowVideoFormatTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(DirectShowVideoFormatTests PRIVATE
            /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME DirectShowVideoFormatTests COMMAND DirectShowVideoFormatTests)

    add_executable(DirectShowDevicesTests
        tests/DirectShowDevicesTests.cpp
        src/capture/DirectShowDevices.cpp
    )
    target_include_directories(DirectShowDevicesTests PRIVATE src)
    target_compile_definitions(DirectShowDevicesTests PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(DirectShowDevicesTests PRIVATE
        ole32
        oleaut32
        strmiids
    )
    set_target_properties(DirectShowDevicesTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(DirectShowDevicesTests PRIVATE
            /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME DirectShowDevicesTests COMMAND DirectShowDevicesTests)

    add_executable(SettingsStoreTests
        tests/SettingsStoreTests.cpp
        src/settings/SettingsStore.cpp
    )
    target_include_directories(SettingsStoreTests PRIVATE src)
    target_compile_definitions(SettingsStoreTests PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    set_target_properties(SettingsStoreTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(SettingsStoreTests PRIVATE
            /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME SettingsStoreTests COMMAND SettingsStoreTests)

    add_executable(UpdateCheckerTests
        tests/UpdateCheckerTests.cpp
        src/update/UpdateChecker.cpp
    )
    target_include_directories(UpdateCheckerTests PRIVATE src)
    target_compile_definitions(UpdateCheckerTests PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(UpdateCheckerTests PRIVATE winhttp)
    set_target_properties(UpdateCheckerTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(UpdateCheckerTests PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME UpdateCheckerTests COMMAND UpdateCheckerTests)

    add_executable(WindowGeometryTests
        tests/WindowGeometryTests.cpp
        src/ui/WindowGeometry.cpp
    )
    target_include_directories(WindowGeometryTests PRIVATE src)
    target_compile_definitions(WindowGeometryTests PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    set_target_properties(WindowGeometryTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(WindowGeometryTests PRIVATE
            /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME WindowGeometryTests COMMAND WindowGeometryTests)

    add_executable(AudioErrorHistoryTests
        tests/AudioErrorHistoryTests.cpp
        src/diagnostics/AudioErrorHistory.cpp
    )
    target_include_directories(AudioErrorHistoryTests PRIVATE src)
    set_target_properties(AudioErrorHistoryTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(AudioErrorHistoryTests PRIVATE
            /W4 /EHsc /permissive- /utf-8)
    endif()
    add_test(NAME AudioErrorHistoryTests COMMAND AudioErrorHistoryTests)

    # Keep the formerly failing driver/transition cases in normal regression runs.
    add_test(NAME VideoFormatFaults COMMAND DirectShowVideoFormatTests --faults)
    add_test(NAME VideoTransitionFaults COMMAND AudioCallbackTests --transition-faults)
    add_test(NAME VideoLayoutStress COMMAND DirectShowVideoFormatTests --stress)
    add_test(NAME VideoMailboxStress COMMAND LatestVideoSampleTests --stress)
    add_test(NAME VideoTransitionStress COMMAND AudioCallbackTests --transition-stress)
    set_tests_properties(VideoFormatFaults VideoTransitionFaults VideoLayoutStress
        VideoMailboxStress VideoTransitionStress PROPERTIES TIMEOUT 30)
endif()

# AppSettings is shared across translation units. Localized MSVC include scans
# must not leave stale objects with an older struct layout after adding a field.
get_property(llcv_build_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(llcv_target IN LISTS llcv_build_targets)
    get_target_property(llcv_sources ${llcv_target} SOURCES)
    if("src/ui/SettingsView.cpp" IN_LIST llcv_sources OR
       "src/ui/ViewerHelpWindow.cpp" IN_LIST llcv_sources OR
       "src/ui/AudioOnlyView.cpp" IN_LIST llcv_sources)
        target_sources(${llcv_target} PRIVATE src/ui/SettingsFonts.cpp src/ui/SettingsFonts.rc)
    endif()
    if("src/ui/SettingsTheme.cpp" IN_LIST llcv_sources)
        target_link_libraries(${llcv_target} PRIVATE uxtheme)
    endif()
    foreach(llcv_source IN LISTS llcv_sources)
        if(llcv_source MATCHES "\\.cpp$")
            set_property(SOURCE ${llcv_source} APPEND PROPERTY OBJECT_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/src/settings/AppSettings.h")
        endif()
    endforeach()
endforeach()

install(TARGETS LowLatencyCaptureViewer RUNTIME DESTINATION .)
install(FILES LICENSE README.md README.ko.md DESIGN.txt BUILD_INFO.txt DEPENDENCIES.txt
        "실행안내.txt" GITHUB_PREP.md DESTINATION .)
install(FILES third_party/asio/LICENSE.txt DESTINATION . RENAME ASIO-SDK-LICENSE.txt)
install(FILES third_party/asio/HOST-LICENSE.txt DESTINATION . RENAME ASIO-HOST-LICENSE.txt)
install(FILES third_party/pretendard/LICENSE.txt DESTINATION . RENAME PRETENDARD-LICENSE.txt)
install(DIRECTORY docs DESTINATION .)
