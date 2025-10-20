#include <GL/glew.h>

#include "application.hpp"
#include "window.hpp"
#include "shader.hpp"
#include "sphere.hpp"
#include "font_overlay.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <random>
#include <vector>
#include <system_error>


namespace {

// --- CONSTANTS ---
constexpr float kSphereRadius = 1737.4f;
constexpr float kMinCameraDistance = 1750.0f;
constexpr float kMaxCameraDistance = 20000.0f;
constexpr float kScrollMinSpeed = 1.0f;
constexpr float kScrollMaxSpeed = 2000.0f;
constexpr float kOrbitMinSpeedDegreesPerSecond =  0.2f;
constexpr float kOrbitMaxSpeedDegreesPerSecond = 90.0f;
constexpr float kDistanceChangePerSecond = 1500.0f;
constexpr float kPitchLimitDegrees = 89.0f;
constexpr std::size_t kMaxTerrainTilesCached = 256;

// --- SHADERS ---

const char* kVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;

out vec3 vNormal;
out vec3 vFragPos;
out vec3 vColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(model))) * aNormal;
    vColor = aColor;
    gl_Position = projection * view * worldPos;
}
)";

const char* kFragmentShaderSource = R"(
#version 330 core
in vec3 vNormal;
in vec3 vFragPos;
in vec3 vColor;

out vec4 FragColor;

uniform vec3 lightDirection;
uniform vec3 cameraPosition;

void main() {
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(lightDirection);
    float diff = max(dot(norm, lightDir), 0.0);

    float ambient = 0.18;

    vec3 viewDir = normalize(cameraPosition - vFragPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 32.0) * 0.25;

    vec3 litColor = vColor * (ambient + diff * 0.82) + spec;
    FragColor = vec4(litColor, 1.0);
}
)";

}  // namespace

class SphereViewerApp : public Application {
public:
    SphereViewerApp() : Application("Sphere Tile Viewer") {
        camera->target = glm::vec3(0.0f);
        camera->distance = 6000.0f;
        camera->yaw = -90.0f;
        camera->pitch = 20.0f;
        camera->updateVectors();
        screenSize_ = glm::vec2(static_cast<float>(window->currentWidth),
                                static_cast<float>(window->currentHeight));
    }

protected:
    void setup() override {
        setupCallbacks();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        shader = std::make_unique<ShaderProgram>(kVertexShaderSource, kFragmentShaderSource);

        const std::string terrainRoot = locateTerrainDataRoot();
        if (!terrainRoot.empty()) {
            std::cout << "Terrain data root: " << terrainRoot << std::endl;
            sphere_ = std::make_unique<Sphere>(kSphereRadius, terrainRoot, kMaxTerrainTilesCached);
        } else {
            std::cout << "Terrain data root not found; rendering base sphere." << std::endl;
            sphere_ = std::make_unique<Sphere>(kSphereRadius);
        }

        shader->use();
        modelLoc_ = shader->getUniformLocation("model");
        viewLoc_ = shader->getUniformLocation("view");
        projectionLoc_ = shader->getUniformLocation("projection");
        lightDirLoc_ = shader->getUniformLocation("lightDirection");
        cameraPosLoc_ = shader->getUniformLocation("cameraPosition");

        fpsOverlay_.initialize("fonts/ProggyClean.ttf");
        fpsOverlay_.setScreenSize(screenSize_);

    }

    void update(float deltaTime) override {
        handleCameraInput(deltaTime);
        sphere_->updateLODs(camera.get(), screenSize_, false);
        fpsOverlay_.update(deltaTime);
    }

    void render() override {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        
        shader->use();

        const glm::mat4 model(1.0f);
        const glm::mat4 view = getViewMatrix();
        const glm::mat4 projection = getProjectionMatrix();

        glUniformMatrix4fv(modelLoc_, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc_, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc_, 1, GL_FALSE, glm::value_ptr(projection));

        const glm::vec3 fromCenter = glm::normalize(camera->position - camera->target);
        const glm::vec3 right = glm::normalize(glm::cross(fromCenter, camera->worldUp));
        const glm::vec3 up = glm::normalize(glm::cross(right, fromCenter));
        const glm::vec3 lightDirection = glm::normalize(fromCenter + 0.5f * right + 0.2f * up);

        glUniform3fv(lightDirLoc_, 1, glm::value_ptr(lightDirection));
        glUniform3fv(cameraPosLoc_, 1, glm::value_ptr(camera->position));

        glPolygonMode(GL_FRONT_AND_BACK, wireframeEnabled_ ? GL_LINE : GL_FILL);
        sphere_->draw();

        fpsOverlay_.render();

    }

    void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods) override {
        if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
            wireframeEnabled_ = !wireframeEnabled_;
            wireframeMode = wireframeEnabled_;
            glPolygonMode(GL_FRONT_AND_BACK, wireframeEnabled_ ? GL_LINE : GL_FILL);
            input->handleKeyPress(key);
            return;
        }
        Application::keyCallback(w, key, scancode, action, mods);
    }

    void framebufferSizeCallback(GLFWwindow* w, int width, int height) override {
        Application::framebufferSizeCallback(w, width, height);
        screenSize_.x = static_cast<float>(std::max(width, 1));
        screenSize_.y = static_cast<float>(std::max(height, 1));
    fpsOverlay_.setScreenSize(screenSize_);
    // We don't need to rebuild geometry here, the projection matrix will handle it.
    }

    void scrollCallback(GLFWwindow* /*w*/, double /*xoffset*/, double yoffset) override {
        const float speed = computeScrollZoomSpeed();
        camera->distance = std::clamp(
            camera->distance - static_cast<float>(yoffset) * speed,
            kMinCameraDistance,
            kMaxCameraDistance);
        camera->updateVectors();
    }

    void mouseCallback(GLFWwindow* /*w*/, double xpos, double ypos) override {
        glm::vec2 mouseDelta = input->getMouseDelta(xpos, ypos);

        if (input->leftMousePressed) {
            const float orbitSpeed = computeOrbitSpeed();
            const float yawDelta = mouseDelta.x * camera->sensitivity * orbitSpeed * 0.01f;
            const float pitchDelta = mouseDelta.y * camera->sensitivity * orbitSpeed * 0.01f;
            camera->yaw -= yawDelta;
            camera->pitch -= pitchDelta;
            camera->pitch = std::clamp(camera->pitch, -kPitchLimitDegrees, kPitchLimitDegrees);
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

    void printControls() override {
        std::cout << "\n=== Sphere Viewer Controls ===" << std::endl;
        std::cout << "W/S: Pitch camera" << std::endl;
        std::cout << "A/D: Yaw camera" << std::endl;
        std::cout << "R/F: Increase/Decrease orbit distance" << std::endl;
        std::cout << "TAB: Toggle wireframe" << std::endl;
        std::cout << "ESC: Quit" << std::endl;
        std::cout << "==============================\n" << std::endl;
    }

private:
    std::string locateTerrainDataRoot() const {
        std::vector<std::filesystem::path> candidates;
        //candidates.emplace_back(".data/proc");  // currently out because it is very inefficient

        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (!candidate.empty() && std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
                auto normalized = std::filesystem::weakly_canonical(candidate, ec);
                if (ec) {
                    normalized = candidate.lexically_normal();
                }
                return normalized.string();
            }
        }
        return {};
    }

    void handleCameraInput(float deltaTime) {
        bool updated = false;
        const float orbitSpeed = computeOrbitSpeed();
        const float scrollSpeed = computeScrollZoomSpeed();

        if (input->isKeyPressed(GLFW_KEY_W)) {
            camera->pitch += orbitSpeed * deltaTime;
            updated = true;
        }
        if (input->isKeyPressed(GLFW_KEY_S)) {
            camera->pitch -= orbitSpeed * deltaTime;
            updated = true;
        }
        if (input->isKeyPressed(GLFW_KEY_A)) {
            camera->yaw -= orbitSpeed * deltaTime;
            updated = true;
        }
        if (input->isKeyPressed(GLFW_KEY_D)) {
            camera->yaw += orbitSpeed * deltaTime;
            updated = true;
        }

        camera->pitch = std::clamp(camera->pitch, -kPitchLimitDegrees, kPitchLimitDegrees);

        if (input->isKeyPressed(GLFW_KEY_R)) {
            camera->distance = std::max(camera->distance - kDistanceChangePerSecond * deltaTime * scrollSpeed*0.01f, kMinCameraDistance);
            updated = true;
        }
        if (input->isKeyPressed(GLFW_KEY_F)) {
            camera->distance = std::min(camera->distance + kDistanceChangePerSecond * deltaTime * scrollSpeed*0.01f, kMaxCameraDistance);
            updated = true;
        }

        if (updated) {
            camera->updateVectors();
        }
    }

    std::unique_ptr<Sphere> sphere_;
    bool wireframeEnabled_ = false;
    FontOverlay fpsOverlay_;
    glm::vec2 screenSize_{static_cast<float>(Window::DEFAULT_WIDTH), static_cast<float>(Window::DEFAULT_HEIGHT)};

    GLint modelLoc_ = -1;
    GLint viewLoc_ = -1;
    GLint projectionLoc_ = -1;
    GLint lightDirLoc_ = -1;
    GLint cameraPosLoc_ = -1;

    float computeScrollZoomSpeed() const {
        const float range = std::max(kMaxCameraDistance - kMinCameraDistance, 1.0f);
        float ratio = (camera->distance - kMinCameraDistance) / range;
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        const float eased = powf(ratio, 0.707f); // * ratio;
        return kScrollMinSpeed + (kScrollMaxSpeed - kScrollMinSpeed) * eased;
    }

    float computeOrbitSpeed() const {
        const float range = std::max(kMaxCameraDistance - kMinCameraDistance, 1.0f);
        float ratio = (camera->distance - kMinCameraDistance) / range;
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        const float eased = powf(ratio, 0.707f); // * ratio;
        return kOrbitMinSpeedDegreesPerSecond +
               (kOrbitMaxSpeedDegreesPerSecond - kOrbitMinSpeedDegreesPerSecond) * eased;
    }
};

int main() {
    try {
        SphereViewerApp app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

