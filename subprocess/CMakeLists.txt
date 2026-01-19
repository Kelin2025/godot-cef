cmake_minimum_required(VERSION 3.21)
project(cef_subprocess)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use static runtime to match CEF
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# CEF location (relative to parent)
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../cef")
set(CEF_WRAPPER_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../cef_wrapper_build/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib")

if(NOT EXISTS "${CEF_ROOT}/include/cef_app.h")
    message(FATAL_ERROR "CEF not found at ${CEF_ROOT}")
endif()

if(NOT EXISTS "${CEF_WRAPPER_LIB}")
    message(FATAL_ERROR "CEF wrapper library not found at ${CEF_WRAPPER_LIB}")
endif()

# Create subprocess executable
add_executable(${PROJECT_NAME} WIN32 cef_subprocess.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CEF_ROOT}
)

# Link CEF libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CEF_ROOT}/Release/libcef.lib
    ${CEF_WRAPPER_LIB}
)

# CEF standard Windows libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    comctl32.lib
    gdi32.lib
    rpcrt4.lib
    shlwapi.lib
    ws2_32.lib
    Advapi32.lib
    dbghelp.lib
    Delayimp.lib
    ntdll.lib
    OleAut32.lib
    PowrProf.lib
    Propsys.lib
    psapi.lib
    SetupAPI.lib
    Shell32.lib
    Shcore.lib
    Userenv.lib
    version.lib
    wbemuuid.lib
    winmm.lib
)

# Compiler definitions matching CEF
target_compile_definitions(${PROJECT_NAME} PRIVATE
    __STDC_CONSTANT_MACROS
    __STDC_FORMAT_MACROS
    WIN32
    _WIN32
    _WINDOWS
    UNICODE
    _UNICODE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _HAS_EXCEPTIONS=0
    PSAPI_VERSION=1
)

# Disable iterator debugging in debug to match CEF
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:_HAS_ITERATOR_DEBUGGING=0>
)

# Match CEF compile flags
target_compile_options(${PROJECT_NAME} PRIVATE
    /MP
    /Gy
    /W3
    /wd4100
    /wd4127
    /wd4244
    /wd4324
    /wd4481
    /wd4512
    /wd4701
    /wd4702
    /wd4996
    /Zi
)

# Output to addons folder alongside main dll
# CMAKE_CURRENT_SOURCE_DIR is .../shared/cef_webview/subprocess
# We need to go up 3 levels to project root, then into addons
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../../../addons/cef_webview/bin/windows"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/../../../addons/cef_webview/bin/windows"
)
