#pragma once

#include <GLFW/glfw3.h>
#include <iostream>

class Window {
  public:
    static constexpr int DEFAULT_WIDTH = 1920;
    static constexpr int DEFAULT_HEIGHT = 1080;

    int windowedPosX = 100;
    int windowedPosY = 100;
    int windowedWidth = DEFAULT_WIDTH;
    int windowedHeight = DEFAULT_HEIGHT;

    int currentWidth = DEFAULT_WIDTH;
    int currentHeight = DEFAULT_HEIGHT;

    bool isFullscreen = false;
    GLFWwindow* handle = nullptr;

    Window(const char* title = "Viewer") {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SAMPLES, 4); // 4x MSAA

        handle = glfwCreateWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, title, nullptr, nullptr);
        if (!handle) {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }

        glfwMakeContextCurrent(handle);
        glfwSwapInterval(1); // Enable VSync
        glfwGetFramebufferSize(handle, &currentWidth, &currentHeight);
        glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    ~Window() {
        if (handle) {
            glfwDestroyWindow(handle);
            glfwTerminate();
        }
    }

    void toggleFullscreen() {
        if (isFullscreen) {
            // Switch to windowed mode
            glfwSetWindowMonitor(handle, nullptr, windowedPosX, windowedPosY, windowedWidth, windowedHeight,
                                 GLFW_DONT_CARE);
            isFullscreen = false;
            std::cout << "Switched to windowed mode (" << windowedWidth << "x" << windowedHeight << ")" << std::endl;
        } else {
            // Save current windowed position and size
            glfwGetWindowPos(handle, &windowedPosX, &windowedPosY);
            glfwGetWindowSize(handle, &windowedWidth, &windowedHeight);

            // Switch to fullscreen on primary monitor
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);

            glfwSetWindowMonitor(handle, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            isFullscreen = true;
            std::cout << "Switched to fullscreen mode (" << mode->width << "x" << mode->height << " @ "
                      << mode->refreshRate << "Hz)" << std::endl;
        }
    }

    void updateFramebufferSize(int width, int height) {
        currentWidth = width;
        currentHeight = height;
    }

    float getAspectRatio() const {
        return static_cast<float>(currentWidth) / static_cast<float>(currentHeight);
    }

    bool shouldClose() const {
        return glfwWindowShouldClose(handle);
    }

    void swapBuffers() {
        glfwSwapBuffers(handle);
    }

    void pollEvents() {
        glfwPollEvents();
    }
};
