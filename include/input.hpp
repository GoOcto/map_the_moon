#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class InputHandler {
public:
    bool keys[1024] = {false};
    bool firstMouse = true;
    bool leftMousePressed = false;
    bool rightMousePressed = false;
    bool middleMousePressed = false;
    float lastX = 0.0f;
    float lastY = 0.0f;
    
    InputHandler(float windowWidth, float windowHeight) 
        : lastX(windowWidth / 2.0f), lastY(windowHeight / 2.0f) {
    }
    
    void handleKeyPress(int key) {
        keys[key] = true;
    }
    
    void handleKeyRelease(int key) {
        keys[key] = false;
    }
    
    void handleMouseButton(int button, bool pressed) {
        switch (button) {
            case GLFW_MOUSE_BUTTON_LEFT:
                leftMousePressed = pressed;
                break;
            case GLFW_MOUSE_BUTTON_RIGHT:
                rightMousePressed = pressed;
                break;
            case GLFW_MOUSE_BUTTON_MIDDLE:
                middleMousePressed = pressed;
                break;
        }
    }
    
    glm::vec2 getMouseDelta(double xpos, double ypos) {
        if (firstMouse) {
            lastX = static_cast<float>(xpos);
            lastY = static_cast<float>(ypos);
            firstMouse = false;
            return glm::vec2(0.0f, 0.0f);
        }
        
        float xoffset = static_cast<float>(xpos) - lastX;
        float yoffset = lastY - static_cast<float>(ypos); // Reversed (Y grows downward)
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        
        return glm::vec2(xoffset, yoffset);
    }
    
    bool isKeyPressed(int key) const {
        return keys[key];
    }
};
