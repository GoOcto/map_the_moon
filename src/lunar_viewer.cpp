/*
 * Lunar Surface Viewer - High Performance OpenGL Implementation
 * Real-time 3D visualization with game-like controls
 */

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <vector>
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>

// Constants
const int WINDOW_WIDTH = 1920;
const int WINDOW_HEIGHT = 1080;
const int MESH_SIZE = 1024;
const int FULL_WIDTH = 23040;
const int FULL_HEIGHT = 15360;

// Camera state
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
    bool orbitMode;
    
    Camera() : position(512.0f, 512.0f, 500.0f),
               front(0.0f, 0.0f, -1.0f),
               up(0.0f, 1.0f, 0.0f),
               worldUp(0.0f, 0.0f, 1.0f),
               yaw(-90.0f),
               pitch(-20.0f),
               speed(50.0f),
               sensitivity(0.15f),
               fov(45.0f),
               target(512.0f, 512.0f, 0.0f),
               distance(500.0f),
               orbitMode(true) {
        updateVectors();
    }
    
    void updateVectors() {
        if (orbitMode) {
            // Orbit camera around target point
            glm::vec3 newPos;
            newPos.x = target.x + distance * cos(glm::radians(yaw)) * cos(glm::radians(pitch));
            newPos.y = target.y + distance * sin(glm::radians(yaw)) * cos(glm::radians(pitch));
            newPos.z = target.z + distance * sin(glm::radians(pitch));
            position = newPos;
            front = glm::normalize(target - position);
            right = glm::normalize(glm::cross(front, worldUp));
            up = glm::normalize(glm::cross(right, front));
        } else {
            // Free fly camera (FPS mode)
            glm::vec3 newFront;
            newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
            newFront.y = sin(glm::radians(pitch));
            newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
            front = glm::normalize(newFront);
            right = glm::normalize(glm::cross(front, worldUp));
            up = glm::normalize(glm::cross(right, front));
        }
    }
};

Camera camera;
bool firstMouse = true;
bool leftMousePressed = false;
bool rightMousePressed = false;
bool middleMousePressed = false;
float lastX = WINDOW_WIDTH / 2.0f;
float lastY = WINDOW_HEIGHT / 2.0f;
float deltaTime = 0.0f;
float lastFrame = 0.0f;
bool wireframeMode = false;
bool keys[1024] = {false};

// Vertex shader source
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aElevation;

out float elevation;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    elevation = aElevation;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

// Fragment shader source with terrain coloring
const char* fragmentShaderSource = R"(
#version 330 core
in float elevation;
in vec3 FragPos;
out vec4 FragColor;

uniform float minElevation;
uniform float maxElevation;

vec3 getTerrainColor(float normalized) {
    // Terrain colormap: deep blue -> green -> brown -> white
    vec3 colors[5];
    colors[0] = vec3(0.1, 0.2, 0.5);  // Deep (low elevation)
    colors[1] = vec3(0.3, 0.5, 0.3);  // Green
    colors[2] = vec3(0.6, 0.5, 0.3);  // Brown
    colors[3] = vec3(0.8, 0.8, 0.7);  // Light
    colors[4] = vec3(1.0, 1.0, 1.0);  // White (high elevation)
    
    float scaled = normalized * 4.0;
    int idx = int(floor(scaled));
    idx = clamp(idx, 0, 3);
    float t = scaled - float(idx);
    
    return mix(colors[idx], colors[idx + 1], t);
}

void main() {
    float normalized = (elevation - minElevation) / (maxElevation - minElevation);
    normalized = clamp(normalized, 0.0, 1.0);
    
    vec3 color = getTerrainColor(normalized);
    
    // Simple lighting
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 normal = normalize(cross(dFdx(FragPos), dFdy(FragPos)));
    float diff = max(dot(normal, lightDir), 0.0) * 0.6 + 0.4;
    
    FragColor = vec4(color * diff, 1.0);
}
)";

// Load lunar elevation data
std::vector<float> loadLunarData(const char* filepath, int& outWidth, int& outHeight) {
    std::cout << "Loading lunar data from: " << filepath << std::endl;
    
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file: " << filepath << std::endl;
        return {};
    }
    
    // Load center region
    int centerX = FULL_WIDTH / 2;
    int centerY = FULL_HEIGHT / 2;
    int startX = centerX - MESH_SIZE / 2;
    int startY = centerY - MESH_SIZE / 2;
    
    std::vector<float> data(MESH_SIZE * MESH_SIZE);
    
    for (int y = 0; y < MESH_SIZE; y++) {
        // Seek to correct row
        size_t offset = ((startY + y) * FULL_WIDTH + startX) * sizeof(float);
        file.seekg(offset);
        
        // Read row
        file.read(reinterpret_cast<char*>(&data[y * MESH_SIZE]), MESH_SIZE * sizeof(float));
        
        if (y % 256 == 0) {
            std::cout << "  Loading row " << y << "/" << MESH_SIZE << std::endl;
        }
    }
    
    file.close();
    
    // Convert from km to meters
    for (float& val : data) {
        val *= 1000.0f;
    }
    
    outWidth = MESH_SIZE;
    outHeight = MESH_SIZE;
    
    // Find min/max
    float minElev = *std::min_element(data.begin(), data.end());
    float maxElev = *std::max_element(data.begin(), data.end());
    std::cout << "Elevation range: " << minElev << " to " << maxElev << " meters" << std::endl;
    
    return data;
}

// Generate mesh vertices and indices
void generateMesh(const std::vector<float>& elevationData, int width, int height,
                  std::vector<float>& vertices, std::vector<unsigned int>& indices) {
    
    std::cout << "Generating mesh..." << std::endl;
    
    float scaleZ = 0.001f; // Vertical exaggeration
    
    // Generate vertices (position + elevation)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float elevation = elevationData[y * width + x];
            
            // Position
            vertices.push_back(static_cast<float>(x));
            vertices.push_back(static_cast<float>(y));
            vertices.push_back(elevation * scaleZ);
            
            // Elevation (for coloring)
            vertices.push_back(elevation);
        }
    }
    
    // Generate indices for triangle strip
    for (int y = 0; y < height - 1; y++) {
        for (int x = 0; x < width - 1; x++) {
            int topLeft = y * width + x;
            int topRight = topLeft + 1;
            int bottomLeft = (y + 1) * width + x;
            int bottomRight = bottomLeft + 1;
            
            // First triangle
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            
            // Second triangle
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }
    
    std::cout << "Generated " << vertices.size() / 4 << " vertices and " 
              << indices.size() / 3 << " triangles" << std::endl;
}

// Compile shader
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compilation failed:\n" << infoLog << std::endl;
    }
    
    return shader;
}

// Keyboard callback
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        keys[key] = true;
        
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, true);
        } else if (key == GLFW_KEY_TAB) {
            wireframeMode = !wireframeMode;
            glPolygonMode(GL_FRONT_AND_BACK, wireframeMode ? GL_LINE : GL_FILL);
        } else if (key == GLFW_KEY_SPACE) {
            // Toggle between orbit and free fly mode
            camera.orbitMode = !camera.orbitMode;
            if (camera.orbitMode) {
                // Switching to orbit mode - set target to current look point
                camera.target = camera.position + camera.front * 100.0f;
                camera.distance = glm::length(camera.target - camera.position);
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                std::cout << "Orbit mode enabled (mouse to rotate)" << std::endl;
            } else {
                // Switching to FPS mode
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                std::cout << "FPS mode enabled (WASD to move, mouse to look)" << std::endl;
            }
        } else if (key == GLFW_KEY_R) {
            // Reset camera
            camera.position = glm::vec3(512.0f, 512.0f, 500.0f);
            camera.target = glm::vec3(512.0f, 512.0f, 0.0f);
            camera.yaw = -90.0f;
            camera.pitch = -20.0f;
            camera.distance = 500.0f;
            camera.fov = 45.0f;
            camera.updateVectors();
            std::cout << "Camera reset" << std::endl;
        }
    } else if (action == GLFW_RELEASE) {
        keys[key] = false;
    }
}

// Mouse button callback
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) leftMousePressed = true;
        if (button == GLFW_MOUSE_BUTTON_RIGHT) rightMousePressed = true;
        if (button == GLFW_MOUSE_BUTTON_MIDDLE) middleMousePressed = true;
    } else if (action == GLFW_RELEASE) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) leftMousePressed = false;
        if (button == GLFW_MOUSE_BUTTON_RIGHT) rightMousePressed = false;
        if (button == GLFW_MOUSE_BUTTON_MIDDLE) middleMousePressed = false;
    }
}

// Mouse callback
void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
        return;
    }
    
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // Reversed (Y grows downward)
    lastX = xpos;
    lastY = ypos;
    
    if (camera.orbitMode) {
        // Orbit mode - only rotate when left mouse is pressed
        if (leftMousePressed) {
            xoffset *= camera.sensitivity;
            yoffset *= camera.sensitivity;
            
            camera.yaw += xoffset;
            camera.pitch += yoffset;
            
            // Constrain pitch to avoid gimbal lock
            if (camera.pitch > 89.0f) camera.pitch = 89.0f;
            if (camera.pitch < -89.0f) camera.pitch = -89.0f;
            
            camera.updateVectors();
        }
        
        // Pan with right mouse button
        if (rightMousePressed) {
            float panSpeed = 0.5f;
            glm::vec3 right = glm::normalize(glm::cross(camera.front, camera.worldUp));
            glm::vec3 up = glm::normalize(glm::cross(right, camera.front));
            camera.target -= right * xoffset * panSpeed;
            camera.target -= up * yoffset * panSpeed;
            camera.updateVectors();
        }
        
        // Pan with middle mouse button (alternative)
        if (middleMousePressed) {
            float panSpeed = 0.5f;
            glm::vec3 right = glm::normalize(glm::cross(camera.front, camera.worldUp));
            glm::vec3 up = glm::normalize(glm::cross(right, camera.front));
            camera.target -= right * xoffset * panSpeed;
            camera.target -= up * yoffset * panSpeed;
            camera.updateVectors();
        }
    } else {
        // FPS mode - always rotate (cursor is disabled)
        xoffset *= camera.sensitivity;
        yoffset *= camera.sensitivity;
        
        camera.yaw += xoffset;
        camera.pitch += yoffset;
        
        // Constrain pitch
        if (camera.pitch > 89.0f) camera.pitch = 89.0f;
        if (camera.pitch < -89.0f) camera.pitch = -89.0f;
        
        camera.updateVectors();
    }
}

// Scroll callback for FOV/zoom
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (camera.orbitMode) {
        // Zoom by changing distance to target
        camera.distance -= static_cast<float>(yoffset) * 20.0f;
        if (camera.distance < 10.0f) camera.distance = 10.0f;
        if (camera.distance > 2000.0f) camera.distance = 2000.0f;
        camera.updateVectors();
    } else {
        // Change FOV in FPS mode
        camera.fov -= static_cast<float>(yoffset) * 2.0f;
        if (camera.fov < 1.0f) camera.fov = 1.0f;
        if (camera.fov > 90.0f) camera.fov = 90.0f;
    }
}

// Process continuous input
void processInput(GLFWwindow* window) {
    float velocity = camera.speed * deltaTime;
    
    if (camera.orbitMode) {
        // In orbit mode, WASD moves the target point
        if (keys[GLFW_KEY_W]) camera.target += glm::vec3(0.0f, velocity, 0.0f);
        if (keys[GLFW_KEY_S]) camera.target -= glm::vec3(0.0f, velocity, 0.0f);
        if (keys[GLFW_KEY_A]) camera.target -= glm::vec3(velocity, 0.0f, 0.0f);
        if (keys[GLFW_KEY_D]) camera.target += glm::vec3(velocity, 0.0f, 0.0f);
        if (keys[GLFW_KEY_Q]) camera.target -= glm::vec3(0.0f, 0.0f, velocity);
        if (keys[GLFW_KEY_E]) camera.target += glm::vec3(0.0f, 0.0f, velocity);
        
        // Arrow keys to adjust distance
        if (keys[GLFW_KEY_UP]) camera.distance -= velocity * 2.0f;
        if (keys[GLFW_KEY_DOWN]) camera.distance += velocity * 2.0f;
        if (camera.distance < 10.0f) camera.distance = 10.0f;
        if (camera.distance > 2000.0f) camera.distance = 2000.0f;
        
        camera.updateVectors();
    } else {
        // FPS mode - WASD moves camera position
        if (keys[GLFW_KEY_W]) camera.position += camera.front * velocity;
        if (keys[GLFW_KEY_S]) camera.position -= camera.front * velocity;
        if (keys[GLFW_KEY_A]) camera.position -= camera.right * velocity;
        if (keys[GLFW_KEY_D]) camera.position += camera.right * velocity;
        if (keys[GLFW_KEY_Q]) camera.position -= camera.up * velocity;
        if (keys[GLFW_KEY_E]) camera.position += camera.up * velocity;
    }
    
    // Shift to speed up
    if (keys[GLFW_KEY_LEFT_SHIFT]) {
        camera.speed = 150.0f;
    } else {
        camera.speed = 50.0f;
    }
}

int main(int argc, char** argv) {
    const char* filepath = ".data/dem/SLDEM2015_512_00N_30N_000_045_FLOAT.IMG";
    if (argc > 1) {
        filepath = argv[1];
    }
    
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4); // 4x MSAA
    
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, 
                                          "Lunar Surface Viewer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    
    // Start in orbit mode with normal cursor
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }
    
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    
    // Load data
    int width, height;
    std::vector<float> elevationData = loadLunarData(filepath, width, height);
    if (elevationData.empty()) {
        return -1;
    }
    
    float minElev = *std::min_element(elevationData.begin(), elevationData.end());
    float maxElev = *std::max_element(elevationData.begin(), elevationData.end());
    
    // Generate mesh
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    generateMesh(elevationData, width, height, vertices, indices);
    
    // Create VAO, VBO, EBO
    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    
    glBindVertexArray(VAO);
    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), 
                 vertices.data(), GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
                 indices.data(), GL_STATIC_DRAW);
    
    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Elevation attribute
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 
                         (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // Create shader program
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    // Get uniform locations
    GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
    GLint viewLoc = glGetUniformLocation(shaderProgram, "view");
    GLint projectionLoc = glGetUniformLocation(shaderProgram, "projection");
    GLint minElevLoc = glGetUniformLocation(shaderProgram, "minElevation");
    GLint maxElevLoc = glGetUniformLocation(shaderProgram, "maxElevation");
    
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
    std::cout << "  Mouse: Look around (cursor hidden)" << std::endl;
    std::cout << "  WASD: Move forward/back/left/right" << std::endl;
    std::cout << "  Q/E: Move down/up" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Other:" << std::endl;
    std::cout << "  Shift: Move faster" << std::endl;
    std::cout << "  Tab: Toggle wireframe" << std::endl;
    std::cout << "  ESC: Quit" << std::endl;
    std::cout << "===============\n" << std::endl;
    
    // Main render loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        
        processInput(window);
        
        // Clear
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Use shader
        glUseProgram(shaderProgram);
        
        // Set uniforms
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = glm::lookAt(camera.position, camera.position + camera.front, camera.up);
        glm::mat4 projection = glm::perspective(glm::radians(camera.fov),
                                               (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT,
                                               0.1f, 10000.0f);
        
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform1f(minElevLoc, minElev);
        glUniform1f(maxElevLoc, maxElev);
        
        // Draw mesh
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    // Cleanup
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(shaderProgram);
    
    glfwTerminate();
    return 0;
}
