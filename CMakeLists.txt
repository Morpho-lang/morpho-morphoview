cmake_minimum_required(VERSION 3.13)
project(morpho-morphoview)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_executable(morphoview "") 
add_subdirectory(src)

# Add glad headers
add_subdirectory(deps/glad)
target_include_directories(morphoview PUBLIC deps/glad/include)

# Locate the morpho.h header file and store in MORPHO_HEADER
find_file(MORPHO_HEADER
          morpho.h 
          HINTS 
          /usr/local/opt/morpho
          /opt/homebrew/opt/morpho
          /usr/local/include/morpho
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
)

# Locate cblas
find_library(CBLAS_LIBRARY
    NAMES cblas libcblas blas libblas
)

# Locate a lapack version
find_library(LAPACK_LIBRARY
    NAMES lapacke liblapacke lapack liblapack 
)

# Locate freetype
find_package(Freetype REQUIRED)
target_include_directories(morphoview PRIVATE ${FREETYPE_INCLUDE_DIRS})

# Locate glfw3
find_package(glfw3 3.3 REQUIRED)

# Link with math library [needed on linux]
IF (NOT WIN32)
  target_link_libraries(morphoview m)
ENDIF()

target_link_libraries(morphoview ${MORPHO_LIBRARY} ${FREETYPE_LIBRARIES} glfw ${CBLAS_LIBRARY} ${LAPACK_LIBRARY})

# Install the resulting binary
install(TARGETS morphoview)