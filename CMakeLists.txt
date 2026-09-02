#-------------------------------------------------------------------------------
# Morpho-morphoview/CMakeLists.txt
#-------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.13)

# Single-config generators (Unix Makefiles, Ninja) default to an empty
# CMAKE_BUILD_TYPE, which means no -O3. Multi-config generators (Xcode, VS)
# ignore CMAKE_BUILD_TYPE; --config selects the variant at build time.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "CMAKE_BUILD_TYPE unset; defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

project(morpho-morphoview)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_executable(morphoview "")

#-------------------------------------------------------------------------------
# Release LTO
#-------------------------------------------------------------------------------

include(CheckIPOSupported)
check_ipo_supported(RESULT MORPHOVIEW_IPO_SUPPORTED OUTPUT MORPHOVIEW_IPO_OUTPUT LANGUAGES C)
if(MORPHOVIEW_IPO_SUPPORTED)
    set_property(TARGET morphoview PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_property(TARGET morphoview PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    message(STATUS "IPO/LTO enabled for Release")
else()
    message(STATUS "IPO/LTO not enabled (${MORPHOVIEW_IPO_OUTPUT})")
endif() 

#-------------------------------------------------------------------------------
# Include the morpho library
#-------------------------------------------------------------------------------

# Locate the morpho.h header file and store in MORPHO_HEADER
find_file(MORPHO_HEADER
          morpho.h 
          HINTS 
          /usr/local/include/
          /usr/local/include/morpho
          /opt/homebrew/include/
          /opt/homebrew/include/morpho
          /home/linuxbrew/.linuxbrew/include/
          /home/linuxbrew/.linuxbrew/include/morpho
          "C:/Program Files/Morpho/include/morpho"
          )

# Identify folder that morpho.h is located in from MORPHO_HEADER and store in MORPHO_INCLUDE
get_filename_component(MORPHO_INCLUDE ${MORPHO_HEADER} DIRECTORY)

# Add morpho headers to MORPHO_INCLUDE
target_include_directories(morphoview PUBLIC ${MORPHO_INCLUDE})

# Add morpho headers in subfolders to MORPHO_INCLUDE
file(GLOB morpho_subdirectories LIST_DIRECTORIES true ${MORPHO_INCLUDE}/*)
foreach(dir ${morpho_subdirectories})
    IF(IS_DIRECTORY ${dir})
        target_include_directories(morphoview PUBLIC ${dir})
    ELSE()
        CONTINUE()
    ENDIF()
endforeach()

# Locate libmorpho
find_library(MORPHO_LIBRARY
    NAMES morpho libmorpho
    PATHS
    "C:/Program Files/Morpho/lib/" 
)

target_link_libraries(morphoview PRIVATE ${MORPHO_LIBRARY})

#-------------------------------------------------------------------------------
# GLFW and Freetype
#-------------------------------------------------------------------------------

# Locate glfw3
find_package(glfw3 3.3 REQUIRED)
target_link_libraries(morphoview PRIVATE glfw)

# Locate freetype
find_package(Freetype REQUIRED)
target_link_libraries(morphoview PRIVATE Freetype::Freetype)

#-------------------------------------------------------------------------------
# ZeroMQ / CZMQ
#-------------------------------------------------------------------------------

find_path(CZMQ_INCLUDE_DIR czmq.h
    HINTS
        /usr/local/include
        /opt/homebrew/include
        /home/linuxbrew/.linuxbrew/include
        "C:/Program Files/Morpho/include"
)
find_library(CZMQ_LIBRARY
    NAMES czmq libczmq
    HINTS
        /usr/local/lib
        /opt/homebrew/lib
        /home/linuxbrew/.linuxbrew/lib
        "C:/Program Files/Morpho/lib"
)
find_library(ZMQ_LIBRARY
    NAMES zmq libzmq
    HINTS
        /usr/local/lib
        /opt/homebrew/lib
        /home/linuxbrew/.linuxbrew/lib
        "C:/Program Files/Morpho/lib"
)

if(NOT CZMQ_INCLUDE_DIR OR NOT CZMQ_LIBRARY)
    message(FATAL_ERROR "czmq not found. Install czmq (e.g. brew install czmq / apt install libczmq-dev).")
endif()

target_include_directories(morphoview PRIVATE ${CZMQ_INCLUDE_DIR})
target_link_libraries(morphoview PRIVATE ${CZMQ_LIBRARY})
if(ZMQ_LIBRARY)
    target_link_libraries(morphoview PRIVATE ${ZMQ_LIBRARY})
endif()
message(STATUS "Found czmq at ${CZMQ_LIBRARY}")

find_package(Threads REQUIRED)
target_link_libraries(morphoview PRIVATE Threads::Threads)

# Link with math library [needed on linux]
IF (NOT WIN32)
  target_link_libraries(morphoview PRIVATE m)
ENDIF()

#-------------------------------------------------------------------------------
# Include source files
#-------------------------------------------------------------------------------

add_subdirectory(src)

# Add glad headers
add_subdirectory(deps/glad)
target_include_directories(morphoview PUBLIC deps/glad/include)

#-------------------------------------------------------------------------------
# Install
#-------------------------------------------------------------------------------

# When ON, `cmake --install` places the binary in <source>/bin (package-local),
# leaving the usual build/ artifact in place. When OFF, install uses the
# normal prefix (e.g. /usr/local/bin) as before.
option(MORPHOVIEW_LOCAL_INSTALL
    "Install morphoview into the package bin/ directory instead of the system prefix"
    ON)

if(MORPHOVIEW_LOCAL_INSTALL)
    install(TARGETS morphoview RUNTIME DESTINATION "${PROJECT_SOURCE_DIR}/bin")
    message(STATUS "MORPHOVIEW_LOCAL_INSTALL: will install to ${PROJECT_SOURCE_DIR}/bin")
else()
    install(TARGETS morphoview RUNTIME DESTINATION bin)
endif()