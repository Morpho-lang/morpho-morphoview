#-------------------------------------------------------------------------------
# morpho-morphoview/CMakeLists.txt
#-------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.13)

# Default to a Release build when no build type is set.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "CMAKE_BUILD_TYPE unset; defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

project(morpho-morphoview LANGUAGES C)

add_executable(morphoview "")

# Record link directories in the installed executable's rpath.
set_target_properties(morphoview PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE)

# Build as a Windows executable, but retain main() as the entry point.
if(WIN32)
    set_target_properties(morphoview PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(morphoview PRIVATE "/ENTRY:mainCRTStartup")
    endif()
endif()

#-------------------------------------------------------------------------------
# Release LTO
#-------------------------------------------------------------------------------

include(CheckIPOSupported)

check_ipo_supported(
    RESULT MORPHOVIEW_IPO_SUPPORTED
    OUTPUT MORPHOVIEW_IPO_OUTPUT
    LANGUAGES C
)

if(MORPHOVIEW_IPO_SUPPORTED)
    set_property(TARGET morphoview PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    message(STATUS "IPO/LTO enabled for Release")
else()
    message(STATUS "IPO/LTO not enabled (${MORPHOVIEW_IPO_OUTPUT})")
endif()

#-------------------------------------------------------------------------------
# Sources
#-------------------------------------------------------------------------------

add_subdirectory(src)

add_subdirectory(deps/glad)
target_include_directories(morphoview PRIVATE deps/glad/include)

#-------------------------------------------------------------------------------
# Dependencies
#-------------------------------------------------------------------------------

find_package(morpho CONFIG REQUIRED)

find_package(glfw3 3.3 REQUIRED)
find_package(Freetype REQUIRED)

# Locate both libraries when the CZMQ package is absent.
find_package(czmq CONFIG QUIET)
if(NOT TARGET czmq)
    find_path(CZMQ_INCLUDE_DIR NAMES czmq.h)
    find_library(CZMQ_LIBRARY NAMES czmq libczmq)
    find_path(ZMQ_INCLUDE_DIR NAMES zmq.h)
    find_library(ZMQ_LIBRARY NAMES zmq libzmq)

    if(NOT CZMQ_INCLUDE_DIR OR NOT CZMQ_LIBRARY
            OR NOT ZMQ_INCLUDE_DIR OR NOT ZMQ_LIBRARY)
        message(FATAL_ERROR "Could not find CZMQ and ZeroMQ.")
    endif()
endif()

find_package(Threads REQUIRED)

find_library(MATH_LIBRARY m)

#-------------------------------------------------------------------------------
# Linking
#-------------------------------------------------------------------------------

target_link_libraries(morphoview PRIVATE
    morpho::morpho
    glfw
    Freetype::Freetype
    Threads::Threads
)

if(TARGET czmq)
    target_link_libraries(morphoview PRIVATE czmq)
else()
    target_include_directories(morphoview PRIVATE
        ${CZMQ_INCLUDE_DIR}
        ${ZMQ_INCLUDE_DIR}
    )
    target_link_libraries(morphoview PRIVATE ${CZMQ_LIBRARY} ${ZMQ_LIBRARY})
endif()

if(MATH_LIBRARY)
    target_link_libraries(morphoview PRIVATE ${MATH_LIBRARY})
endif()

#-------------------------------------------------------------------------------
# Installation
#-------------------------------------------------------------------------------

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}"
        CACHE PATH "Install prefix" FORCE)
endif()

install(TARGETS morphoview
    RUNTIME DESTINATION bin
)

# Install required DLLs next to the executable.
if(WIN32 AND DEFINED VCPKG_TARGET_TRIPLET)
    if(DEFINED VCPKG_INSTALLED_DIR)
        set(_mv_vcpkg_dll_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    else()
        set(_mv_vcpkg_dll_dir "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin")
    endif()
    if(EXISTS "${_mv_vcpkg_dll_dir}")
        install(DIRECTORY "${_mv_vcpkg_dll_dir}/"
                DESTINATION bin
                FILES_MATCHING PATTERN "*.dll")
        message(STATUS "Will install vcpkg DLLs from ${_mv_vcpkg_dll_dir}")
    endif()
endif()
