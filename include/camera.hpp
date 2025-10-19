#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

struct Camera {
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;
    float yaw;
    float pitch;
    float speed;
    float sensitivity;
    float fov;
    
    // Orbit mode
    glm::vec3 target;
    float distance;

    Camera(float initialDistance = 500.0f) : 
               worldUp(0.0f, 0.0f, 1.0f),
               speed(50.0f),
               sensitivity(0.15f),
               fov(45.0f),
               distance(initialDistance) {
        reset();
        updateVectors();
    }

    virtual void reset() {
        position = glm::vec3(0.0f, 0.0f, distance);
        front = glm::vec3(0.0f, 0.0f, -1.0f);
        up = glm::vec3(0.0f, 1.0f, 0.0f);
        target = glm::vec3(0.0f, 0.0f, 0.0f);
        yaw = -90.0f;
        pitch = 20.0f;
        updateVectors();
    }
    
    void updateVectors() {
        // Orbit camera around target point
        glm::vec3 newPos;
        newPos.x = target.x + distance * cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        newPos.y = target.y + distance * sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        newPos.z = target.z + distance * sin(glm::radians(pitch));
        position = newPos;
        front = glm::normalize(target - position);
        right = glm::normalize(glm::cross(front, worldUp));
        up = glm::normalize(glm::cross(right, front));
    }
    
    void constrainPitch() {
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }
};
