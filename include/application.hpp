#pragma once

#include "window.hpp"
#include "camera.hpp"
#include "input.hpp"
#include "shader.hpp"
#include "mesh.hpp"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <chrono>

class Application {
protected:
    std::unique_ptr<Window> window;
    std::unique_ptr<InputHandler> input;
    std::unique_ptr<Camera> camera;
    std::unique_ptr<ShaderProgram> shader;
    std::unique_ptr<Mesh> mesh;
    
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    bool wireframeMode = false;
    
    // Callback storage for GLFW
    static Application* instancePtr;
    
public:
    Application(const char* windowTitle) {
        instancePtr = this;
        
        try {
            window = std::make_unique<Window>(windowTitle);
            input = std::make_unique<InputHandler>((float)Window::DEFAULT_WIDTH, (float)Window::DEFAULT_HEIGHT);
            camera = std::make_unique<Camera>();

            initializeGL();
            
            mesh = std::make_unique<Mesh>();  // this line makes the exe exit with code 0xC0000005, notice it does not get caught by the catch below
        } catch (const std::exception& e) {
            std::cerr << "Failed to initialize application: " << e.what() << std::endl;
            throw;
        }
        
        
    }
    
    virtual ~Application() {
        instancePtr = nullptr;
    }
    
    void initializeGL() {
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) {
            throw std::runtime_error("Failed to initialize GLEW");
        }
        
        glViewport(0, 0, window->currentWidth, window->currentHeight);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_MULTISAMPLE);
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    }
    
    virtual void setup() = 0;
    virtual void update(float deltaTime) = 0;
    virtual void render() = 0;
    
    void run() {
        setup();
        printControls();
        
        while (!window->shouldClose()) {
            updateTime();
            
            input->handleKeyPress(GLFW_KEY_SPACE); // This will be overridden by actual input
            
            update(deltaTime);
            
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            render();
            
            window->swapBuffers();
            window->pollEvents();
        }
    }
    
    virtual void printControls() {
        std::cout << "\n=== Controls ===" << std::endl;
        std::cout << "Camera Modes:" << std::endl;
        std::cout << "  Space: Toggle Orbit/FPS mode" << std::endl;
        std::cout << "  R: Reset camera" << std::endl;
        std::cout << "" << std::endl;
        std::cout << "Orbit Mode (default):" << std::endl;
        std::cout << "  Left-click + drag: Rotate around terrain" << std::endl;
        std::cout << "  Right-click + drag: Pan camera" << std::endl;
        std::cout << "  Scroll: Zoom in/out" << std::endl;
        std::cout << "  WASD: Move target point" << std::endl;
        std::cout << "" << std::endl;
        std::cout << "FPS Mode:" << std::endl;
        std::cout << "  Mouse: Look around" << std::endl;
        std::cout << "  WASD: Move forward/back/left/right" << std::endl;
        std::cout << "  Q/E: Move down/up" << std::endl;
        std::cout << "" << std::endl;
        std::cout << "Other:" << std::endl;
        std::cout << "  Shift: Move faster" << std::endl;
        std::cout << "  Tab: Toggle wireframe" << std::endl;
        std::cout << "  F11 or Ctrl+Enter: Toggle fullscreen" << std::endl;
        std::cout << "  ESC: Quit" << std::endl;
        std::cout << "===============\n" << std::endl;
    }
    
    // GLFW Callback setters
    static void glfwKeyCallback(GLFWwindow* w, int key, int scancode, int action, int mods) {
        if (instancePtr) instancePtr->keyCallback(w, key, scancode, action, mods);
    }
    
    static void glfwMouseButtonCallback(GLFWwindow* w, int button, int action, int mods) {
        if (instancePtr) instancePtr->mouseButtonCallback(w, button, action, mods);
    }
    
    static void glfwMouseCallback(GLFWwindow* w, double xpos, double ypos) {
        if (instancePtr) instancePtr->mouseCallback(w, xpos, ypos);
    }
    
    static void glfwScrollCallback(GLFWwindow* w, double xoffset, double yoffset) {
        if (instancePtr) instancePtr->scrollCallback(w, xoffset, yoffset);
    }
    
    static void glfwFramebufferSizeCallback(GLFWwindow* w, int width, int height) {
        if (instancePtr) instancePtr->framebufferSizeCallback(w, width, height);
    }
    
    // Virtual callback implementations for subclasses
    virtual void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods) {
        if (action == GLFW_PRESS) {
            input->handleKeyPress(key);
            
            if (key == GLFW_KEY_ESCAPE) {
                glfwSetWindowShouldClose(window->handle, true);
            } else if (key == GLFW_KEY_F11) {
                window->toggleFullscreen();
            } else if (key == GLFW_KEY_ENTER && (mods & GLFW_MOD_CONTROL)) {
                window->toggleFullscreen();
            } else if (key == GLFW_KEY_TAB) {
                wireframeMode = !wireframeMode;
                glPolygonMode(GL_FRONT_AND_BACK, wireframeMode ? GL_LINE : GL_FILL);
            } else if (key == GLFW_KEY_R) {
                camera->reset();
                camera->updateVectors();
                std::cout << "Camera reset" << std::endl;
            }
        } else if (action == GLFW_RELEASE) {
            input->handleKeyRelease(key);
        }
    }
    
    virtual void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods) {
        bool pressed = (action == GLFW_PRESS);
        input->handleMouseButton(button, pressed);
    }
    
    virtual void mouseCallback(GLFWwindow* w, double xpos, double ypos) {
        glm::vec2 mouseDelta = input->getMouseDelta(xpos, ypos);
        
        if (input->leftMousePressed) {
            camera->yaw -= mouseDelta.x * camera->sensitivity;
            camera->pitch -= mouseDelta.y * camera->sensitivity;
            camera->constrainPitch();
            camera->updateVectors();
        }
        
        if (input->rightMousePressed || input->middleMousePressed) {
            float panSpeed = 0.5f;
            glm::vec3 right = glm::normalize(glm::cross(camera->front, camera->worldUp));
            glm::vec3 up = glm::normalize(glm::cross(right, camera->front));
            camera->target -= right * mouseDelta.x * panSpeed;
            camera->target -= up * mouseDelta.y * panSpeed;
            camera->updateVectors();
        }
    }
    
    virtual void scrollCallback(GLFWwindow* w, double xoffset, double yoffset) {
        camera->distance -= static_cast<float>(yoffset) * 20.0f;
        if (camera->distance < 10.0f) camera->distance = 10.0f;
        if (camera->distance > 2000.0f) camera->distance = 2000.0f;
        camera->updateVectors();
    }
    
    virtual void framebufferSizeCallback(GLFWwindow* w, int width, int height) {
        window->updateFramebufferSize(width, height);
        glViewport(0, 0, width, height);
    }
    
    glm::mat4 getProjectionMatrix() const {
        return glm::perspective(glm::radians(camera->fov),
                               window->getAspectRatio(),
                               0.1f, 10000.0f);
    }
    
    glm::mat4 getViewMatrix() const {
        return glm::lookAt(camera->position, camera->position + camera->front, camera->up);
    }
    
    void setupCallbacks() {
        glfwSetKeyCallback(window->handle, glfwKeyCallback);
        glfwSetCursorPosCallback(window->handle, glfwMouseCallback);
        glfwSetScrollCallback(window->handle, glfwScrollCallback);
        glfwSetMouseButtonCallback(window->handle, glfwMouseButtonCallback);
        glfwSetFramebufferSizeCallback(window->handle, glfwFramebufferSizeCallback);
    }
    
protected:
    void updateTime() {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
    }
};

// Static member initialization
Application* Application::instancePtr = nullptr;
