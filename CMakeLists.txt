cmake_minimum_required(VERSION 3.10)
project(LunarViewer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenGL (usually available on Windows)
find_package(OpenGL REQUIRED)

# Set library paths for Windows
set(LIBS_DIR ${CMAKE_SOURCE_DIR}/libs)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# GLEW (link statically to avoid runtime DLL dependency)
set(GLEW_INCLUDE_DIR ${LIBS_DIR}/glew/include)
set(GLEW_LIBRARY ${LIBS_DIR}/glew/lib/Release/x64/glew32s.lib)

# GLFW
set(GLFW_INCLUDE_DIR ${LIBS_DIR}/glfw/include)
set(GLFW_LIBRARY ${LIBS_DIR}/glfw/lib/glfw3.lib)

# GLM (header-only)
set(GLM_INCLUDE_DIR ${LIBS_DIR}/glm/include)

# STB
set(STB_INCLUDE_DIR ${LIBS_DIR}/stb)

set(TINYTIFF_DIR ${LIBS_DIR}/TinyTIFF)
set(TINYTIFF_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${TINYTIFF_GENERATED_DIR})
string(TIMESTAMP TINYTIFF_COMPILETIME "%Y-%m-%d")
set(TINYTIFF_VERSION "3.0.0")
set(TINYTIFF_GITVERSION "local")
configure_file(${TINYTIFF_DIR}/tinytiff_version.h.in ${TINYTIFF_GENERATED_DIR}/tinytiff_version.h @ONLY)

add_library(tinytiff STATIC
    ${TINYTIFF_DIR}/tinytiffreader.c
    ${TINYTIFF_DIR}/tinytiff_ctools_internal.c
)
set_source_files_properties(
    ${TINYTIFF_DIR}/tinytiffreader.c
    ${TINYTIFF_DIR}/tinytiff_ctools_internal.c
    PROPERTIES LANGUAGE C
)
target_include_directories(tinytiff PUBLIC
    ${TINYTIFF_DIR}
    ${TINYTIFF_GENERATED_DIR}
)
target_compile_definitions(tinytiff PRIVATE
    _CRT_SECURE_NO_WARNINGS
    HAVE_FTELLI64
)
set_target_properties(tinytiff PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)

# Common source files
set(COMMON_SOURCES
    src/color_map_sampler.cpp
)

# Add executables
add_executable(view_tile src/view_tile.cpp ${COMMON_SOURCES})
add_executable(view_sphere src/view_sphere.cpp ${COMMON_SOURCES})

# Include directories for view_tile
target_include_directories(view_tile PRIVATE
    ${INCLUDE_DIR}
    ${GLEW_INCLUDE_DIR}
    ${GLFW_INCLUDE_DIR}
    ${GLM_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
)

# Link libraries for view_tile
target_link_libraries(view_tile PRIVATE  # <-- Added PRIVATE keyword
    OpenGL::GL
    ${GLEW_LIBRARY}
    ${GLFW_LIBRARY}
    tinytiff
    # REMOVED generator expressions for windows libs 
)

target_compile_definitions(view_tile PRIVATE GLEW_STATIC)


# Set output directory
set_target_properties(view_tile PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Include directories for sphere
target_include_directories(view_sphere PRIVATE
    ${INCLUDE_DIR}
    ${GLEW_INCLUDE_DIR}
    ${GLFW_INCLUDE_DIR}
    ${GLM_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
)

# Link libraries for view_sphere
target_link_libraries(view_sphere PRIVATE  # <-- Added PRIVATE keyword
    OpenGL::GL
    ${GLEW_LIBRARY}
    ${GLFW_LIBRARY}
    tinytiff
    # REMOVED generator expressions for windows libs 
)

target_compile_definitions(view_sphere PRIVATE GLEW_STATIC)

# Set output directory for view_sphere
set_target_properties(view_sphere PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)