cmake_minimum_required(VERSION 3.16)

# Use MSVC for optimal binary size
if(WIN32 AND NOT DEFINED CMAKE_C_COMPILER)
    message(STATUS "Using default MSVC compiler for minimal binary size")
endif()

project(muxsw VERSION 1.0.0 LANGUAGES C)

# MVP Configuration Option
option(MUXSW_ENABLE_AUDIO "Enable audio capture functionality" OFF)

# Windows-only optimized build
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Set output directory to release folder at root
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/release)

# Ensure release directory exists
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/release)

# Compiler flags optimized for MSVC minimal size and stealth
set(CMAKE_C_FLAGS "/W4 /TC /GA")
set(CMAKE_CXX_FLAGS "/W4 /TP /GA")
# Enhanced aggressive size optimization flags
set(CMAKE_C_FLAGS_RELEASE "/Os /DNDEBUG /GL /Gy /GS- /Gm- /fp:fast /MT /Ox /Ob2 /Oi /GF /Gr")
set(CMAKE_CXX_FLAGS_RELEASE "/Os /DNDEBUG /GL /Gy /GS- /Gm- /fp:fast /MT /Ox /Ob2 /Oi /GF /Gr")
set(CMAKE_C_FLAGS_DEBUG "/Od /Zi /DDEBUG /MTd")
set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /DDEBUG /MTd")
# Enhanced linker flags for minimal size and stealth
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF /MERGE:.rdata=.text /MERGE:.pdata=.text /SUBSYSTEM:CONSOLE /FILEALIGN:512")
add_definitions(-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DCOBJMACROS -DCINTERFACE)

# Aggressive size optimization definitions
add_definitions(-DVC_EXTRALEAN -D_WIN32_WINNT=0x0A00 -DNOSERVICE -DNOMCX -DNOIME -DNOSOUND -DNOCOMM -DNOKANJI -DNOHELP -DNOPROFILER)

# MVP Audio Configuration
if(MUXSW_ENABLE_AUDIO)
    add_definitions(-DMUXSW_ENABLE_AUDIO)
    message(STATUS "Audio capture: ENABLED")
else()
    message(STATUS "Audio capture: DISABLED (MVP mode)")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files (refactored modular structure)
set(SOURCES
    src/main.c
    src/screen.c
    src/system.c
    src/encoder.c
    src/engine.c
    src/guids.c
    src/record.c
    src/params.c
    src/signals.c
    src/arguments.c
    src/callbacks.c
    src/filename.c
    src/system_utils.c
)

# Add audio sources conditionally
if(MUXSW_ENABLE_AUDIO)
    list(APPEND SOURCES src/microphone.c)
endif()

# GUI source files (refactored modular structure)
set(GUI_SOURCES
    src/gui.c
    src/screen.c
    src/system.c
    src/encoder.c
    src/engine.c
    src/guids.c
    src/record.c
    src/params.c
    src/signals.c
    src/arguments.c
    src/callbacks.c
    src/filename.c
    src/system_utils.c
)

# Add audio sources conditionally for GUI
if(MUXSW_ENABLE_AUDIO)
    list(APPEND GUI_SOURCES src/microphone.c)
    list(APPEND GUI_SOURCES src/gui_callbacks.c)
else()
    list(APPEND GUI_SOURCES src/gui_callbacks.c)
endif()

# Create console executable
add_executable(muxsw ${SOURCES})

# Create GUI executable  
add_executable(muxsw-gui WIN32 ${GUI_SOURCES})

# Windows libraries
set(WINDOWS_LIBS 
    d3d11 
    dxgi 
    ole32 
    oleaut32 
    winmm
    # Additional required libraries
    uuid
    # DXGI and Audio IID constants
    dxguid
    # Additional GUID library
    strmiids
    # DXGI functions need these
    kernel32
    user32
    gdi32
    winspool
    comdlg32
    advapi32
    shell32
    # Windows Media Foundation for encoding
    mf
    mfplat
    mfreadwrite
    mfuuid
)

# Add audio libraries conditionally
if(MUXSW_ENABLE_AUDIO)
    list(APPEND WINDOWS_LIBS
        # Core Audio API constants
        mmdevapi
        # KS media format GUIDs
        ksuser
    )
endif()

# Link libraries
target_link_libraries(muxsw
    ${WINDOWS_LIBS}
)

# Link libraries for GUI
target_link_libraries(muxsw-gui
    ${WINDOWS_LIBS}
    comctl32  # For common controls (progress bar, etc.)
    shlwapi   # For path functions
)

# Set proper subsystem for GUI with minimal manifest
set_target_properties(muxsw-gui PROPERTIES
    LINK_FLAGS "/SUBSYSTEM:WINDOWS"
)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "Using Windows Media Foundation for video encoding")

# Test targets
add_custom_target(test_basic
    COMMAND python ${CMAKE_SOURCE_DIR}/tests/test_muxsw.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running basic test suite"
)

add_custom_target(test_all
    DEPENDS test_basic
    COMMENT "Running all test suites"
)

# Clean target
add_custom_target(clean_all
    COMMENT "Clean all build artifacts and test files"
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/build
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/release
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Install targets
install(TARGETS muxsw muxsw-gui RUNTIME DESTINATION bin)

# Package configuration
set(CPACK_PACKAGE_NAME "muxsw")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Mux Sweeper - High-performance screen and audio capture tool")
set(CPACK_PACKAGE_VENDOR "muxsw")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

include(CPack)
