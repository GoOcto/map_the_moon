cmake_minimum_required(VERSION 3.10)
project(LunarViewer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# C standard for tinytiff
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Check for 64-bit file functions for TinyTIFF
include(CheckFunctionExists)
check_function_exists(_ftelli64 HAVE_FTELLI64)
check_function_exists(ftello HAVE_FTELLO)

# --- Find System Libraries ---

# Silence OpenGL GLVND vs. Legacy warning (Policy CMP0072)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    cmake_policy(SET CMP0072 NEW)
    set(OpenGL_GL_PREFERENCE "GLVND")
endif()

# This requires GLEW and GLFW development packages to be installed on Linux
# (e.g., sudo apt-get install libglew-dev libglfw3-dev)
find_package(OpenGL REQUIRED)
set(GLEW_STATIC ON) # We want static linking, as in the original
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)

# --- Paths for Bundled Libraries ---
# We'll keep using your bundled versions of GLM, STB, and TinyTIFF source
set(LIBS_DIR ${CMAKE_SOURCE_DIR}/libs)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# Header-only libraries
set(GLM_INCLUDE_DIR ${LIBS_DIR}/glm/include)
set(STB_INCLUDE_DIR ${LIBS_DIR}/stb)

# --- Build TinyTIFF from source ---
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

# Platform-specific definitions for tinytiff
if(HAVE_FTELLI64)
    target_compile_definitions(tinytiff PRIVATE HAVE_FTELLI64)
elseif(HAVE_FTELLO)
    target_compile_definitions(tinytiff PRIVATE HAVE_FTELLO)
endif()

if(MSVC)
    # Add MSVC-specific warning suppression
    target_compile_definitions(tinytiff PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # Add POSIX definition for 64-bit file offsets
    target_compile_definitions(tinytiff PRIVATE _LARGEFILE64_SOURCE=1)
endif()

# --- Executable Targets ---
set(COMMON_SOURCES
    src/color_map_sampler.cpp
)

# Use a loop to define common properties for both executables
foreach(TARGET view_tile view_sphere)
    add_executable(${TARGET} src/${TARGET}.cpp ${COMMON_SOURCES})

    # Include directories for bundled libs + your custom include
    target_include_directories(${TARGET} PRIVATE
        ${INCLUDE_DIR}
        ${GLM_INCLUDE_DIR}
        ${STB_INCLUDE_DIR}
        # Note: GLEW and GLFW includes are added automatically
        # by their imported targets below.
    )

    # Link libraries using modern, cross-platform imported targets
    target_link_libraries(${TARGET} PRIVATE
        OpenGL::GL
        GLEW::glew
        glfw      
        tinytiff
    )

    # Set output directory
    set_target_properties(${TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endforeach()