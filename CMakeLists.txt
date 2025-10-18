cmake_minimum_required(VERSION 3.10)
project(LunarViewer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# Add executable
add_executable(lunar_viewer src/lunar_viewer.cpp)

# Link libraries
target_link_libraries(lunar_viewer
    OpenGL::GL
    GLEW::GLEW
    glfw
    glm::glm
)

# Set output directory
set_target_properties(lunar_viewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Compiler flags for optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(lunar_viewer PRIVATE -O3 -march=native)
endif()
