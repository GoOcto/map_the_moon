/*
 * Lunar Surface Viewer - High Performance OpenGL Implementation
 * Real-time 3D visualization with game-like controls
 */

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "application.hpp"
#include "mesh.hpp"
#include "shader.hpp"
#include "terrain_loader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float kMinDistance = 10.0f;
constexpr float kMaxDistance = 2000.0f;
constexpr float kFpsMoveScale = 0.1f;
constexpr float kScrollZoomFactor = 20.0f;
constexpr float kScrollFovStep = 2.0f;
constexpr float kOrbitPanSpeed = 0.5f;
constexpr float kShiftSpeed = 150.0f;
constexpr float kNormalSpeed = 50.0f;
constexpr float kLightAngleDegrees = 20.0f;
constexpr double kDefaultLatitudeDegrees = 15.0;
constexpr double kDefaultLongitudeDegrees = 22.5;
constexpr double kLatitudeStepDegrees = 0.1;
constexpr double kLongitudeStepDegrees = 0.1;
constexpr double kMinLatitudeDegrees = -60.0;
constexpr double kMaxLatitudeDegrees = 60.0;

double wrapLongitudeDegrees(double lonDegrees) {
    double wrapped = std::fmod(lonDegrees, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return wrapped;
}

const char* kVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aElevation;

out float elevation;
out vec3 FragPos;
out vec3 WorldPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float uCurvature;

void main() {
    const float kHalfWidth = 512.0;
    const float kHalfHeight = 512.0;
    const float kEpsilon = 1e-6;

    vec2 centered = vec2(aPos.x - kHalfWidth, aPos.y - kHalfHeight);
    vec3 curved = vec3(centered, aPos.z);

    if (abs(uCurvature) > kEpsilon) {
        float radius = 1.0 / uCurvature;
        float thetaX = centered.x * uCurvature;
        float thetaY = centered.y * uCurvature;

        float sinX = sin(thetaX);
        float cosX = cos(thetaX);
        float sinY = sin(thetaY);
        float cosY = cos(thetaY);

        curved.x = radius * sinX;
        curved.y = radius * sinY;

        float drop = radius * (2.0 - cosX - cosY);
        curved.z = aPos.z - drop;
    }

    curved.x += kHalfWidth;
    curved.y += kHalfHeight;

    vec4 world = model * vec4(curved, 1.0);
    WorldPos = vec3(world);
    FragPos = WorldPos;
    elevation = aElevation;
    gl_Position = projection * view * world;
}
)";

const char* kFragmentShaderSource = R"(
#version 330 core
in float elevation;
in vec3 FragPos;
in vec3 WorldPos;
out vec4 FragColor;

uniform float minElevation;
uniform float maxElevation;
uniform vec3 lightDirection;

vec3 getTerrainColor(float normalized) {
    vec3 colors[5];
    colors[0] = vec3(0.1, 0.2, 0.5);  // Deep (low elevation)
    colors[1] = vec3(0.3, 0.5, 0.3);  // Green
    colors[2] = vec3(0.6, 0.5, 0.3);  // Brown
    colors[3] = vec3(0.8, 0.8, 0.7);  // Light
    colors[4] = vec3(1.0, 1.0, 1.0);  // White (high elevation)

    vec3 white = vec3(1.0, 1.0, 1.0);

    float scaled = normalized * 4.0;
    int idx = int(floor(scaled));
    idx = clamp(idx, 0, 4);
    float t = scaled - float(idx);

    vec3 mixColor = mix(colors[idx], colors[idx + 1], t) * 0.7 + white * 0.3;
    return mixColor;
}

void main() {
    float normalized = (elevation - minElevation) / (maxElevation - minElevation);
    normalized = clamp(normalized, 0.0, 1.0);

    vec3 baseColor = getTerrainColor(normalized);

    vec3 dFdxPos = dFdx(WorldPos);
    vec3 dFdyPos = dFdy(WorldPos);
    vec3 normal = normalize(cross(dFdxPos, dFdyPos));

    vec3 lightDir = normalize(lightDirection);
    float diff = max(dot(normal, lightDir), 0.0);

    float ambient = 0.25;

    vec3 viewDir = normalize(-FragPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.3;

    vec3 lighting = vec3(ambient + diff * 0.75 + spec);
    vec3 finalColor = baseColor * lighting;

    FragColor = vec4(finalColor, 1.0);
}
)";

} // namespace

class LunarViewerApp : public Application {
public:
    LunarViewerApp(const char* windowTitle, std::string dataRoot)
        : Application(windowTitle), 
          dataRoot_(std::move(dataRoot)) // Store dataRoot
    {
        m_terrain = std::make_unique<TerrainLoader>(dataRoot_);
    }

protected:
    void setup() override {
        setupCallbacks();

        shader = std::make_unique<ShaderProgram>(kVertexShaderSource, kFragmentShaderSource);

        logCurrentCoordinates();
        loadTerrain();
        mesh->uploadData();
        mesh->setupVertexAttributes({3, 1});

        shader->use();
        modelLoc_ = shader->getUniformLocation("model");
        viewLoc_ = shader->getUniformLocation("view");
        projectionLoc_ = shader->getUniformLocation("projection");
        minElevLoc_ = shader->getUniformLocation("minElevation");
        maxElevLoc_ = shader->getUniformLocation("maxElevation");
        lightDirLoc_ = shader->getUniformLocation("lightDirection");
        curvatureLoc_ = shader->getUniformLocation("uCurvature");

        lightDirection_ = glm::normalize(glm::vec3(
            std::cos(glm::radians(kLightAngleDegrees)),
            0.0f,
            std::sin(glm::radians(kLightAngleDegrees))));

        centerCamera();
    }

    void update(float deltaTime) override {
        const float velocity = camera->speed * deltaTime;

        if (input->isKeyPressed(GLFW_KEY_KP_4)) {
            adjustLongitude(-kLongitudeStepDegrees*samplingStep_);
        }
        if (input->isKeyPressed(GLFW_KEY_KP_6)) {
            adjustLongitude(kLongitudeStepDegrees*samplingStep_);
        }
        if (input->isKeyPressed(GLFW_KEY_KP_8)) {
            adjustLatitude(-kLatitudeStepDegrees*samplingStep_);
        }
        if (input->isKeyPressed(GLFW_KEY_KP_2)) {
            adjustLatitude(kLatitudeStepDegrees*samplingStep_);
        }

        if (input->isKeyPressed(GLFW_KEY_UP)) camera->distance -= velocity * 2.0f;
        if (input->isKeyPressed(GLFW_KEY_DOWN)) camera->distance += velocity * 2.0f;
        camera->distance = std::clamp(camera->distance, kMinDistance, kMaxDistance);

        camera->updateVectors();

        camera->speed = input->isKeyPressed(GLFW_KEY_LEFT_SHIFT) ? kShiftSpeed : kNormalSpeed;

        if (needsReload_) {
            reloadTerrain();
        }
    }

    void render() override {
        shader->use();

        const glm::mat4 model = glm::mat4(1.0f);
        const glm::mat4 view = getViewMatrix();
        const glm::mat4 projection = getProjectionMatrix();

        glUniformMatrix4fv(modelLoc_, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc_, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc_, 1, GL_FALSE, glm::value_ptr(projection));

        float kColorMin = -10000.0f / samplingStep_;
        float kColorMax = 10000.0f / samplingStep_;

        glUniform1f(minElevLoc_, kColorMin);
        glUniform1f(maxElevLoc_, kColorMax);
        glUniform3fv(lightDirLoc_, 1, glm::value_ptr(lightDirection_));
        if (curvatureLoc_ != -1) {
            glUniform1f(curvatureLoc_, curvaturePerUnit_);
        }

        mesh->draw();
    }

    void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods) override {
        Application::keyCallback(w, key, scancode, action, mods);

        if (action == GLFW_PRESS) {
            if (key == GLFW_KEY_R) {
                centerCamera();
            }
        }

        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            switch (key) {
                case GLFW_KEY_KP_5:
                    resetViewPosition();
                    break;
                case GLFW_KEY_KP_ADD:
                    samplingStep_ += 1;
                    if (samplingStep_ > 50) samplingStep_ = 50;
                    needsReload_ = true;
                    break;
                case GLFW_KEY_KP_SUBTRACT:
                    samplingStep_ -= 1;
                    if (samplingStep_ < 1) samplingStep_ = 1;
                    needsReload_ = true;
                    break;
                default:
                    break;
            }
        }
    }

    void mouseCallback(GLFWwindow* w, double xpos, double ypos) override {
    }

    void scrollCallback(GLFWwindow* w, double xoffset, double yoffset) override {
    }

private:
    void loadTerrain() {
        elevationData_ = m_terrain->loadOrUpdateTerrain(
            povLatitudeDegrees_, povLongitudeDegrees_, width_, height_, samplingStep_);
        
        if (elevationData_.empty()) {
            throw std::runtime_error("Failed to load terrain data");
        }

        if (samplingStep_ > 1) {
            for (float& val : elevationData_) {
                val *= 1.0f / static_cast<float>(samplingStep_);
            }
        }

        minElevation_ = *std::min_element(elevationData_.begin(), elevationData_.end());
        maxElevation_ = *std::max_element(elevationData_.begin(), elevationData_.end());

        updateCurvatureAmount();

        mesh->vertices.clear();
        mesh->indices.clear();
        TerrainLoader::generateMesh(elevationData_, width_, height_, mesh->vertices, mesh->indices);

        std::cout << "Initial elevation range: " << minElevation_ << " to " << maxElevation_ << " meters" << std::endl;
    }

    void reloadTerrain() {
        needsReload_ = false;

        std::cout << "Reloading terrain data..." << std::endl;
        
        auto newData = m_terrain->loadOrUpdateTerrain(
            povLatitudeDegrees_, povLongitudeDegrees_, width_, height_, samplingStep_);

        if (newData.empty()) {
            std::cerr << "Failed to reload data, keeping previous terrain" << std::endl;
            return;
        }

        elevationData_ = std::move(newData);
        minElevation_ = *std::min_element(elevationData_.begin(), elevationData_.end());
        maxElevation_ = *std::max_element(elevationData_.begin(), elevationData_.end());

        for (float& val : elevationData_) {
            val *= 1.0f / static_cast<float>(samplingStep_);
        }

        updateCurvatureAmount();

        TerrainLoader::updateMeshElevations(elevationData_, width_, height_, mesh->vertices);
        mesh->updateVertexData();
    }

    void centerCamera() {
        camera->target = glm::vec3(
            static_cast<float>(width_) / 2.0f,
            static_cast<float>(height_) / 2.0f,
            0.0f);
        camera->distance = 600.0f;
        camera->yaw = -90.0f;
        camera->pitch = 45.0f;
        camera->updateVectors();
    }

    void adjustLatitude(double deltaDegrees) {
        const double newLat = std::clamp(
            povLatitudeDegrees_ + deltaDegrees, kMinLatitudeDegrees, kMaxLatitudeDegrees);
        if (std::fabs(newLat - povLatitudeDegrees_) > 1e-9) {
            povLatitudeDegrees_ = newLat;
            needsReload_ = true;
            logCurrentCoordinates();
        }
    }

    void adjustLongitude(double deltaDegrees) {
        const double newLon = wrapLongitudeDegrees(povLongitudeDegrees_ + deltaDegrees);
        if (std::fabs(newLon - povLongitudeDegrees_) > 1e-9) {
            povLongitudeDegrees_ = newLon;
            needsReload_ = true;
            logCurrentCoordinates();
        }
    }

    void resetViewPosition() {
        povLatitudeDegrees_ = kDefaultLatitudeDegrees;
        povLongitudeDegrees_ = wrapLongitudeDegrees(kDefaultLongitudeDegrees);
        needsReload_ = true;
        logCurrentCoordinates();
    }

    void logCurrentCoordinates() const {
        std::cout << "View centered at latitude " << povLatitudeDegrees_
                  << " deg, longitude " << povLongitudeDegrees_ << " deg" << std::endl;
    }

    void updateCurvatureAmount() {
        const float degreesPerPixelLon = 45.0f / static_cast<float>(TerrainLoader::TILE_WIDTH);
        const float degreesPerPixelLat = 30.0f / static_cast<float>(TerrainLoader::TILE_HEIGHT);

        const float horizontalSamples = static_cast<float>(width_ - 1) * static_cast<float>(samplingStep_);
        const float verticalSamples = static_cast<float>(height_ - 1) * static_cast<float>(samplingStep_);

        const float lonSpanDegrees = horizontalSamples * degreesPerPixelLon;
        const float latSpanDegrees = verticalSamples * degreesPerPixelLat;

        const float halfWidth = static_cast<float>(width_ - 1) * 0.5f;
        const float halfHeight = static_cast<float>(height_ - 1) * 0.5f;

        float curvatureLon = 0.0f;
        if (halfWidth > 0.0f) {
            curvatureLon = glm::radians(lonSpanDegrees * 0.5f) / halfWidth;
        }

        float curvatureLat = 0.0f;
        if (halfHeight > 0.0f) {
            curvatureLat = glm::radians(latSpanDegrees * 0.5f) / halfHeight;
        }

        curvaturePerUnit_ = std::max(curvatureLon, curvatureLat);
    }

    std::unique_ptr<TerrainLoader> m_terrain;
    std::string dataRoot_;
    std::vector<float> elevationData_;
    
    int width_ = TerrainLoader::MESH_SIZE;
    int height_ = TerrainLoader::MESH_SIZE;

    float minElevation_ = 0.0f;
    float maxElevation_ = 0.0f;

    bool needsReload_ = false;
    double povLatitudeDegrees_ = kDefaultLatitudeDegrees;
    double povLongitudeDegrees_ = wrapLongitudeDegrees(kDefaultLongitudeDegrees);
    int samplingStep_ = 25;

    glm::vec3 lightDirection_{0.0f};

    GLint modelLoc_ = -1;
    GLint viewLoc_ = -1;
    GLint projectionLoc_ = -1;
    GLint minElevLoc_ = -1;
    GLint maxElevLoc_ = -1;
    GLint lightDirLoc_ = -1;
    GLint curvatureLoc_ = -1;
    float curvaturePerUnit_ = 0.0f;
};

int main(int argc, char** argv) {
    const char* defaultDataRoot = ".data/dem";
    std::string dataRoot = (argc > 1) ? argv[1] : defaultDataRoot;

    try {
        LunarViewerApp app("Lunar Surface Viewer", std::move(dataRoot));
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}