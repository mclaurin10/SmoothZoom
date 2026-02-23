cmake_minimum_required(VERSION 3.20)
project(SmoothZoom
    VERSION 0.1.0
    DESCRIPTION "macOS-style full-screen magnification for Windows"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# x64 only (Doc 3 §5.1 — Magnification API not supported under WOW64)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SmoothZoom requires x64. 32-bit builds are not supported.")
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(SMOOTHZOOM_BUILD_TESTS  "Build unit tests"          ON)
option(SMOOTHZOOM_SIGN_BINARY  "Sign output with dev cert"  OFF)

# ---------------------------------------------------------------------------
# Third-party
# ---------------------------------------------------------------------------
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann
)

# ---------------------------------------------------------------------------
# Common (shared types, threading primitives)
# ---------------------------------------------------------------------------
add_library(smoothzoom_common STATIC
    src/common/SeqLock.cpp
    src/common/LockFreeQueue.cpp
    src/common/SharedState.cpp
)
target_include_directories(smoothzoom_common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------------
# Input Layer
# ---------------------------------------------------------------------------
add_library(smoothzoom_input STATIC
    src/input/InputInterceptor.cpp
    src/input/WinKeyManager.cpp
    src/input/FocusMonitor.cpp
    src/input/CaretMonitor.cpp
)
target_include_directories(smoothzoom_input PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(smoothzoom_input PUBLIC smoothzoom_common)

# ---------------------------------------------------------------------------
# Logic Layer
# ---------------------------------------------------------------------------
add_library(smoothzoom_logic STATIC
    src/logic/ZoomController.cpp
    src/logic/ViewportTracker.cpp
    src/logic/RenderLoop.cpp
)
target_include_directories(smoothzoom_logic PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(smoothzoom_logic PUBLIC smoothzoom_common)

# ---------------------------------------------------------------------------
# Output Layer
# ---------------------------------------------------------------------------
add_library(smoothzoom_output STATIC
    src/output/MagBridge.cpp
)
target_include_directories(smoothzoom_output PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(smoothzoom_output PUBLIC smoothzoom_common)

# ---------------------------------------------------------------------------
# Support Layer
# ---------------------------------------------------------------------------
add_library(smoothzoom_support STATIC
    src/support/SettingsManager.cpp
    src/support/TrayUI.cpp
)
target_include_directories(smoothzoom_support PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(smoothzoom_support PUBLIC
    smoothzoom_common
    nlohmann_json
)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(SmoothZoom WIN32
    src/app/main.cpp
)
target_link_libraries(SmoothZoom PRIVATE
    smoothzoom_input
    smoothzoom_logic
    smoothzoom_output
    smoothzoom_support
)

# Win32 system libraries (Doc 3 §5.2)
if(WIN32)
    target_link_libraries(SmoothZoom PRIVATE
        Magnification.lib
        Dwmapi.lib
        User32.lib
        Shell32.lib
        Ole32.lib          # COM for UIA
        OleAut32.lib       # COM for UIA
        UIAutomationCore.lib
    )
endif()

# Embed application manifest (Doc 3 §5.3)
if(MSVC)
    set_target_properties(SmoothZoom PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_SOURCE_DIR}/res/SmoothZoom.manifest"
    )
endif()

# ---------------------------------------------------------------------------
# Phase 0 test harness — standalone risk spike (Doc 4 §3)
# ---------------------------------------------------------------------------
add_executable(Phase0Harness WIN32
    src/app/phase0_harness.cpp
)
if(WIN32)
    target_link_libraries(Phase0Harness PRIVATE
        Magnification.lib
        Dwmapi.lib
        User32.lib
    )
endif()
target_include_directories(Phase0Harness PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
if(MSVC)
    set_target_properties(Phase0Harness PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_SOURCE_DIR}/res/SmoothZoom.manifest"
    )
endif()

# ---------------------------------------------------------------------------
# Unit tests (Layer 1 — pure logic, CI-safe, no Win32 API deps)
# ---------------------------------------------------------------------------
if(SMOOTHZOOM_BUILD_TESTS)
    enable_testing()
    add_executable(smoothzoom_tests
        tests/unit/test_main.cpp
        tests/unit/test_ZoomController.cpp
        tests/unit/test_ViewportTracker.cpp
        tests/unit/test_WinKeyManager.cpp
        tests/unit/test_SettingsManager.cpp
    )
    target_include_directories(smoothzoom_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(smoothzoom_tests PRIVATE
        smoothzoom_logic
        smoothzoom_common
    )
    add_test(NAME UnitTests COMMAND smoothzoom_tests)
endif()
