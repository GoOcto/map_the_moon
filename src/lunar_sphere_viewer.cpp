/*
 * Lunar Sphere Viewer - OpenGL Implementation
 * Displays the Moon as a sphere with radius 1737.4km
 * Uses similar controls to the terrain viewer
 */

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

// Constants
const int WINDOW_WIDTH = 1920;
const int WINDOW_HEIGHT = 1080;
const float MOON_RADIUS = 1737.4f; // km
const int SPHERE_SEGMENTS = 128; // Latitude segments
const int SPHERE_RINGS = 64; // Longitude rings

const float pi_float = 3.14159265358979323846f;

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

    Camera() : 
               worldUp(0.0f, 0.0f, 1.0f),
               speed(500.0f),
               sensitivity(0.15f),
               fov(45.0f),
               orbitMode(true) {
        reset();
        updateVectors();
    }

    void reset() {
        position = glm::vec3(0.0f, 0.0f, MOON_RADIUS * 3.0f);
        front = glm::vec3(0.0f, 0.0f, -1.0f);
        up = glm::vec3(0.0f, 1.0f, 0.0f);
        target = glm::vec3(0.0f, 0.0f, 0.0f);
        yaw = -90.0f;
        pitch = 0.0f;
        distance = MOON_RADIUS * 3.0f;
        updateVectors();
    }
    
    void updateVectors() {
        if (orbitMode) {
            // Orbit camera around target point (Moon center)
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

// Fullscreen state
bool isFullscreen = false;
int windowedPosX = 100;
int windowedPosY = 100;
int windowedWidth = WINDOW_WIDTH;
int windowedHeight = WINDOW_HEIGHT;

// Current framebuffer dimensions
int currentWidth = WINDOW_WIDTH;
int currentHeight = WINDOW_HEIGHT;

// Vertex shader source
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

// Fragment shader source with simple lighting
const char* fragmentShaderSource = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

uniform vec3 lightDirection;
uniform vec3 viewPos;

void main() {
    // Base lunar surface color (gray)
    vec3 baseColor = vec3(0.6, 0.6, 0.6);
    
    // Add subtle variation based on texture coordinates for visual interest
    float variation = sin(TexCoord.x * 50.0) * cos(TexCoord.y * 50.0) * 0.05;
    baseColor += variation;
    
    // Normalize the normal vector
    vec3 norm = normalize(Normal);
    
    // Light direction (Sun-like lighting)
    vec3 lightDir = normalize(lightDirection);
    
    // Diffuse lighting
    float diff = max(dot(norm, lightDir), 0.0);
    
    // Ambient lighting
    float ambient = 0.2;
    
    // Specular highlight (subtle, moon surface is not very reflective)
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 16.0) * 0.1;
    
    // Combine lighting
    vec3 lighting = vec3(ambient + diff * 0.8 + spec);
    
    // Apply lighting to base color
    vec3 finalColor = baseColor * lighting;
    
    FragColor = vec4(finalColor, 1.0);
}
)";

// Generate sphere mesh
void generateSphere(float radius, int segments, int rings,
                   std::vector<float>& vertices, std::vector<unsigned int>& indices) {
    
    std::cout << "Generating sphere mesh..." << std::endl;
    
    // Generate vertices
    for (int ring = 0; ring <= rings; ring++) {
        float phi = pi_float * float(ring) / float(rings); // 0 to PI
        float y = radius * cos(phi);
        float ringRadius = radius * sin(phi);
        
        for (int seg = 0; seg <= segments; seg++) {
            float theta = 2.0f * pi_float * float(seg) / float(segments); // 0 to 2*PI
            float x = ringRadius * cos(theta);
            float z = ringRadius * sin(theta);
            
            // Position
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            
            // Normal (for a sphere at origin, normal is just normalized position)
            glm::vec3 normal = glm::normalize(glm::vec3(x, y, z));
            vertices.push_back(normal.x);
            vertices.push_back(normal.y);
            vertices.push_back(normal.z);
            
            // Texture coordinates
            float u = float(seg) / float(segments);
            float v = float(ring) / float(rings);
            vertices.push_back(u);
            vertices.push_back(v);
        }
    }
    
    // Generate indices
    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            int current = ring * (segments + 1) + seg;
            int next = current + segments + 1;
            
            // First triangle
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);
            
            // Second triangle
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
    
    std::cout << "Generated " << vertices.size() / 8 << " vertices and " 
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

// Toggle fullscreen function
void toggleFullscreen(GLFWwindow* window) {
    if (isFullscreen) {
        // Switch to windowed mode
        glfwSetWindowMonitor(window, nullptr, 
                           windowedPosX, windowedPosY,
                           windowedWidth, windowedHeight,
                           GLFW_DONT_CARE);
        isFullscreen = false;
        std::cout << "Switched to windowed mode (" << windowedWidth << "x" << windowedHeight << ")" << std::endl;
    } else {
        // Save current windowed position and size
        glfwGetWindowPos(window, &windowedPosX, &windowedPosY);
        glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
        
        // Switch to fullscreen on current monitor
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        
        glfwSetWindowMonitor(window, monitor,
                           0, 0,
                           mode->width, mode->height,
                           mode->refreshRate);
        isFullscreen = true;
        std::cout << "Switched to fullscreen mode (" << mode->width << "x" << mode->height << " @ " << mode->refreshRate << "Hz)" << std::endl;
    }
}

// Keyboard callback
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        keys[key] = true;
        
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, true);
        } else if (key == GLFW_KEY_F11) {
            toggleFullscreen(window);
        } else if (key == GLFW_KEY_ENTER && (mods & GLFW_MOD_CONTROL)) {
            toggleFullscreen(window);
        } else if (key == GLFW_KEY_TAB) {
            wireframeMode = !wireframeMode;
            glPolygonMode(GL_FRONT_AND_BACK, wireframeMode ? GL_LINE : GL_FILL);
        } else if (key == GLFW_KEY_SPACE) {
            // Toggle between orbit and free fly mode
            camera.orbitMode = !camera.orbitMode;
            if (camera.orbitMode) {
                camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
                camera.distance = glm::length(camera.position - camera.target);
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                std::cout << "Orbit mode enabled (mouse to rotate)" << std::endl;
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                std::cout << "FPS mode enabled (WASD to move, mouse to look)" << std::endl;
            }
        } else if (key == GLFW_KEY_R) {
            camera.reset();
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
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
        return;
    }
    
    float xoffset = static_cast<float>(xpos) - lastX;
    float yoffset = lastY - static_cast<float>(ypos);
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);
    
    if (camera.orbitMode) {
        if (leftMousePressed) {
            xoffset *= camera.sensitivity;
            yoffset *= camera.sensitivity;
            
            camera.yaw -= xoffset;
            camera.pitch -= yoffset;
            
            if (camera.pitch > 89.0f) camera.pitch = 89.0f;
            if (camera.pitch < -89.0f) camera.pitch = -89.0f;
            
            camera.updateVectors();
        }
        
        if (rightMousePressed || middleMousePressed) {
            float panSpeed = 5.0f;
            glm::vec3 right = glm::normalize(glm::cross(camera.front, camera.worldUp));
            glm::vec3 up = glm::normalize(glm::cross(right, camera.front));
            camera.target -= right * xoffset * panSpeed;
            camera.target -= up * yoffset * panSpeed;
            camera.updateVectors();
        }
    } else {
        xoffset *= camera.sensitivity;
        yoffset *= camera.sensitivity;
        
        camera.yaw += xoffset;
        camera.pitch += yoffset;
        
        if (camera.pitch > 89.0f) camera.pitch = 89.0f;
        if (camera.pitch < -89.0f) camera.pitch = -89.0f;
        
        camera.updateVectors();
    }
}

// Scroll callback for zoom
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (camera.orbitMode) {
        camera.distance -= static_cast<float>(yoffset) * 200.0f;
        if (camera.distance < MOON_RADIUS * 1.1f) camera.distance = MOON_RADIUS * 1.1f;
        if (camera.distance > MOON_RADIUS * 20.0f) camera.distance = MOON_RADIUS * 20.0f;
        camera.updateVectors();
    } else {
        camera.fov -= static_cast<float>(yoffset) * 2.0f;
        if (camera.fov < 1.0f) camera.fov = 1.0f;
        if (camera.fov > 90.0f) camera.fov = 90.0f;
    }
}

// Framebuffer size callback
void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    currentWidth = width;
    currentHeight = height;
    glViewport(0, 0, width, height);
}

// Process continuous input
void processInput(GLFWwindow* window) {
    float velocity = camera.speed * deltaTime;
    
    if (camera.orbitMode) {
        if (keys[GLFW_KEY_W]) camera.target += camera.front * velocity;
        if (keys[GLFW_KEY_S]) camera.target -= camera.front * velocity;
        if (keys[GLFW_KEY_A]) camera.target -= camera.right * velocity;
        if (keys[GLFW_KEY_D]) camera.target += camera.right * velocity;
        if (keys[GLFW_KEY_Q]) camera.target -= camera.up * velocity;
        if (keys[GLFW_KEY_E]) camera.target += camera.up * velocity;
        
        if (keys[GLFW_KEY_UP]) camera.distance -= velocity * 2.0f;
        if (keys[GLFW_KEY_DOWN]) camera.distance += velocity * 2.0f;
        if (camera.distance < MOON_RADIUS * 1.1f) camera.distance = MOON_RADIUS * 1.1f;
        if (camera.distance > MOON_RADIUS * 20.0f) camera.distance = MOON_RADIUS * 20.0f;
        
        camera.updateVectors();
    } else {
        if (keys[GLFW_KEY_W]) camera.position += camera.front * velocity;
        if (keys[GLFW_KEY_S]) camera.position -= camera.front * velocity;
        if (keys[GLFW_KEY_A]) camera.position -= camera.right * velocity;
        if (keys[GLFW_KEY_D]) camera.position += camera.right * velocity;
        if (keys[GLFW_KEY_Q]) camera.position -= camera.up * velocity;
        if (keys[GLFW_KEY_E]) camera.position += camera.up * velocity;
    }
    
    if (keys[GLFW_KEY_LEFT_SHIFT]) {
        camera.speed = 1500.0f;
    } else {
        camera.speed = 500.0f;
    }
}

int main() {
    std::cout << "=== Lunar Sphere Viewer ===" << std::endl;
    std::cout << "Moon radius: " << MOON_RADIUS << " km" << std::endl;
    std::cout << "" << std::endl;
    
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
                                          "Lunar Sphere Viewer", nullptr, nullptr);
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
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    
    // Get actual framebuffer size
    glfwGetFramebufferSize(window, &currentWidth, &currentHeight);
    
    // Start in orbit mode
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }
    
    glViewport(0, 0, currentWidth, currentHeight);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    
    // Generate sphere mesh
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    generateSphere(MOON_RADIUS, SPHERE_SEGMENTS, SPHERE_RINGS, vertices, indices);
    
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 
                         (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // Texture coordinate attribute
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 
                         (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    
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
    GLint lightDirLoc = glGetUniformLocation(shaderProgram, "lightDirection");
    GLint viewPosLoc = glGetUniformLocation(shaderProgram, "viewPos");
    
    // Light direction (Sun-like lighting from the side)
    glm::vec3 lightDirection = glm::normalize(glm::vec3(1.0f, 0.3f, 0.5f));
    
    std::cout << "\n=== Controls ===" << std::endl;
    std::cout << "Camera Modes:" << std::endl;
    std::cout << "  Space: Toggle Orbit/FPS mode" << std::endl;
    std::cout << "  R: Reset camera" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Orbit Mode (default):" << std::endl;
    std::cout << "  Left-click + drag: Rotate around Moon" << std::endl;
    std::cout << "  Right-click + drag: Pan camera" << std::endl;
    std::cout << "  Scroll: Zoom in/out" << std::endl;
    std::cout << "  WASD/QE: Move target point" << std::endl;
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
    
    // Main render loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        
        processInput(window);
        
        // Clear
        glClearColor(0.0f, 0.0f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Use shader
        glUseProgram(shaderProgram);
        
        // Set uniforms
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = glm::lookAt(camera.position, camera.position + camera.front, camera.up);
        glm::mat4 projection = glm::perspective(glm::radians(camera.fov),
                                               (float)currentWidth / (float)currentHeight,
                                               1.0f, MOON_RADIUS * 50.0f);
        
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(lightDirLoc, 1, glm::value_ptr(lightDirection));
        glUniform3fv(viewPosLoc, 1, glm::value_ptr(camera.position));
        
        // Draw sphere
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, 0);
        
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
