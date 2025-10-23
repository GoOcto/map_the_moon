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

# Configure windowing/gl bindings per-platform
set(GLEW_STATIC ON) # We want static linking, as in the original

if(WIN32)
    # Use bundled Windows binaries for GLEW/GLFW
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_ARCH_DIR x64)
    else()
        set(_ARCH_DIR Win32)
    endif()

    set(GLEW_ROOT ${CMAKE_SOURCE_DIR}/libs/glew)
    if(NOT TARGET GLEW::glew)
        add_library(GLEW::glew STATIC IMPORTED)
        set_target_properties(GLEW::glew PROPERTIES
            IMPORTED_LOCATION "${GLEW_ROOT}/lib/Release/${_ARCH_DIR}/glew32s.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${GLEW_ROOT}/include"
        )
        target_compile_definitions(GLEW::glew INTERFACE GLEW_STATIC)
    endif()

    set(GLFW_ROOT ${CMAKE_SOURCE_DIR}/libs/glfw)
    if(NOT TARGET glfw)
        add_library(glfw STATIC IMPORTED)
        set_target_properties(glfw PROPERTIES
            IMPORTED_LOCATION "${GLFW_ROOT}/lib/glfw3_mt.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${GLFW_ROOT}/include"
        )
    endif()

    set(GLFW_SYSTEM_LIBS
        gdi32
        shell32
        user32
        advapi32
        ole32
        oleaut32
        version
        winmm
    )
else()
    find_package(GLEW REQUIRED)
    find_package(glfw3 REQUIRED)

    # Harmonise target name with ours if different
    if(TARGET GLEW::GLEW AND NOT TARGET GLEW::glew)
        add_library(GLEW::glew INTERFACE IMPORTED)
        set_property(TARGET GLEW::glew PROPERTY INTERFACE_LINK_LIBRARIES GLEW::GLEW)
    endif()
endif()

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

    if(WIN32)
        target_link_libraries(${TARGET} PRIVATE ${GLFW_SYSTEM_LIBS})
    endif()

    # Set output directory
    set_target_properties(${TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endforeach()