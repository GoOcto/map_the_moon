cmake_minimum_required(VERSION 3.10)
project(LunarViewer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenGL (usually available on Windows)
find_package(OpenGL REQUIRED)

# Set library paths for Windows
set(LIBS_DIR ${CMAKE_SOURCE_DIR}/libs)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# GLEW
set(GLEW_INCLUDE_DIR ${LIBS_DIR}/glew/include)
set(GLEW_LIBRARY ${LIBS_DIR}/glew/lib/Release/x64/glew32.lib)

# GLFW
set(GLFW_INCLUDE_DIR ${LIBS_DIR}/glfw/include)
set(GLFW_LIBRARY ${LIBS_DIR}/glfw/lib/glfw3.lib)

# GLM (header-only)
set(GLM_INCLUDE_DIR ${LIBS_DIR}/glm/include)

# STB
set(STB_INCLUDE_DIR ${LIBS_DIR}/stb)

# Common source files
set(COMMON_SOURCES
    src/color_map_sampler.cpp
)

# Add executables
add_executable(lunar_viewer src/lunar_viewer.cpp ${COMMON_SOURCES})
add_executable(view_sphere src/view_sphere.cpp ${COMMON_SOURCES})

# Include directories for lunar_viewer
target_include_directories(lunar_viewer PRIVATE
    ${INCLUDE_DIR}
    ${GLEW_INCLUDE_DIR}
    ${GLFW_INCLUDE_DIR}
    ${GLM_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
)

# Link libraries for lunar_viewer
target_link_libraries(lunar_viewer PRIVATE  # <-- Added PRIVATE keyword
    OpenGL::GL
    ${GLEW_LIBRARY}
    ${GLFW_LIBRARY}
    # REMOVED generator expressions for windows libs 
)

if(WIN32)
    target_link_libraries(lunar_viewer PRIVATE
        ole32
        windowscodecs
    )
endif()


# Set output directory
set_target_properties(lunar_viewer PROPERTIES
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
    # REMOVED generator expressions for windows libs 
)

# --- ADDED THIS BLOCK to properly link view_sphere ---
if(WIN32)
    target_link_libraries(view_sphere PRIVATE
        ole32
        windowscodecs
    )
endif()

# Set output directory for view_sphere
set_target_properties(view_sphere PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)