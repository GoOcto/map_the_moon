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
#include "color_map_sampler.hpp"
#include "font_overlay.hpp"
#include "mesh.hpp"
#include "shader.hpp"
#include "terrain_dataset.hpp"
#include "terrain_loader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
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
constexpr float kLightAngleDegrees[] = { 20.0f, 45.0f, 70.0f };
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
layout (location = 2) in vec3 aColor;

out float elevation;
out vec3 FragPos;
out vec3 WorldPos;
out vec3 vertexColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float uCurvature;
uniform vec2 uMeshCenter;

uniform vec2 vNWCorner;
uniform vec2 vSECorner;
uniform uint dimensions;

void main() {
    const float kEpsilon = 1e-6;

    vec2 centered = vec2(aPos.x - uMeshCenter.x, aPos.y - uMeshCenter.y);
    vec3 curved = vec3(centered, aPos.z);

    float curvature = radians((vNWCorner.x - vSECorner.x)) / float(dimensions);
    float lat = vNWCorner.y + (aPos.y / float(dimensions)) * (vSECorner.y - vNWCorner.y);
    float lon = vNWCorner.x + (aPos.x / float(dimensions)) * (vSECorner.x - vNWCorner.x);
    float latCenter = (vNWCorner.y + vSECorner.y) / 2.0;

    if (abs(curvature) > kEpsilon) {
        float radius = 1.0 / curvature;
        float thetaX = centered.x * curvature;
        float thetaY = centered.y * curvature;

        float sinX = sin(thetaX);
        float cosX = cos(thetaX);
        float sinY = sin(thetaY);
        float cosY = cos(thetaY);

        curved.x = radius * sinX;
        curved.y = radius * sinY;

        float drop = radius * (2.0 - cosX - cosY);
        curved.z = aPos.z - drop;
        //curved.x = (curved.x * sin(radians(lat)));
    }

    curved.x += uMeshCenter.x;
    curved.y += uMeshCenter.y;

    vec4 world = model * vec4(curved, 1.0);
    WorldPos = vec3(world);
    FragPos = WorldPos;
    elevation = aElevation;
    vertexColor = aColor;
    gl_Position = projection * view * world;
}
)";

const char* kFragmentShaderSource = R"(
#version 330 core

in float elevation;
in vec3 FragPos;
in vec3 WorldPos;
in vec3 vertexColor;
out vec4 FragColor;

uniform float minElevation;
uniform float maxElevation;
uniform vec3 lightDirection;
uniform float colorMode; // 0.0 for relief coloring, 1.0 for vertex color (from colormap)

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

    vec3 fallbackColor = getTerrainColor(normalized);
    vec3 baseColor = mix(vertexColor, fallbackColor, colorMode);

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
        : Application(windowTitle), dataRoot_(std::move(dataRoot)) // Store dataRoot
    {
        m_colorSampler = std::make_unique<ColorMapSampler>(dataRoot_);
        m_terrain = std::make_unique<TerrainLoader>(dataRoot_);
    }

  protected:
    void setup() override {
        setupCallbacks();

        shader = std::make_unique<ShaderProgram>(kVertexShaderSource, kFragmentShaderSource);

        logCurrentCoordinates();
        loadTerrain();
        mesh->uploadData();
        mesh->setupVertexAttributes({3, 1, 3});

        shader->use();
        modelLoc_ = shader->getUniformLocation("model");
        viewLoc_ = shader->getUniformLocation("view");
        projectionLoc_ = shader->getUniformLocation("projection");
        minElevLoc_ = shader->getUniformLocation("minElevation");
        maxElevLoc_ = shader->getUniformLocation("maxElevation");
        lightDirLoc_ = shader->getUniformLocation("lightDirection");
        colorModeLoc_ = shader->getUniformLocation("colorMode");

        curvatureLoc_ = shader->getUniformLocation("uCurvature");
        vNWCornerLoc_ = shader->getUniformLocation("vNWCorner");
        vSECornerLoc_ = shader->getUniformLocation("vSECorner");
        dimensionsLoc_ = shader->getUniformLocation("dimensions");
        
        meshCenterLoc_ = shader->getUniformLocation("uMeshCenter");

        screenSize_ = glm::vec2(static_cast<float>(window->currentWidth), static_cast<float>(window->currentHeight));
        fpsOverlay_.initialize(dataRoot_ + "fonts/ProggyClean.ttf");
        fpsOverlay_.setScreenSize(screenSize_);

        lightDirection_ = glm::normalize(
            glm::vec3(glm::radians(kLightAngleDegrees[0]), glm::radians(kLightAngleDegrees[1]), glm::radians(kLightAngleDegrees[2])));

        centerCamera();
    }

    void update(float deltaTime) override {
        const float velocity = camera->speed * deltaTime;

        // if (input->isKeyPressed(GLFW_KEY_UP)) camera->distance -= velocity * 2.0f;
        // if (input->isKeyPressed(GLFW_KEY_DOWN)) camera->distance += velocity * 2.0f;
        // camera->distance = std::clamp(camera->distance, kMinDistance, kMaxDistance);
        // camera->updateVectors();
        // camera->speed = input->isKeyPressed(GLFW_KEY_LEFT_SHIFT) ? kShiftSpeed : kNormalSpeed;
        fpsOverlay_.update(deltaTime);

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

        float kColorMin = -10.0f / samplingStep_;
        float kColorMax = 10.0f / samplingStep_;

        glUniform1f(minElevLoc_, kColorMin);
        glUniform1f(maxElevLoc_, kColorMax);
        glUniform3fv(lightDirLoc_, 1, glm::value_ptr(lightDirection_));
        glUniform1f(colorModeLoc_, colorMode_);

        glUniform1f(curvatureLoc_, curvaturePerUnit_);
        glUniform2f(meshCenterLoc_, static_cast<float>(width_) / 2.0f, static_cast<float>(height_) / 2.0f);

        glUniform2fv(vNWCornerLoc_, 1, glm::value_ptr(nwCorner));
        glUniform2fv(vSECornerLoc_, 1, glm::value_ptr(seCorner));
        glUniform1ui(dimensionsLoc_, static_cast<GLuint>(width_));  // assuming square mesh

        mesh->draw();
        updateOverlayStatus();
        fpsOverlay_.render();
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

            case GLFW_KEY_1:
                //glUniform1f(colorModeLoc_, 0.0f);
                colorMode_ = 0.0f;
                break;
            case GLFW_KEY_2:
                //glUniform1f(colorModeLoc_, 1.0f);
                colorMode_ = 1.0f;
                break;

            case GLFW_KEY_KP_1:
                adjustLatitude(-kLatitudeStepDegrees * samplingStep_);
                adjustLongitude(-kLongitudeStepDegrees * samplingStep_);
                break;
            case GLFW_KEY_KP_2:
                adjustLatitude(-kLatitudeStepDegrees * samplingStep_);
                break;
            case GLFW_KEY_KP_3:
                adjustLatitude(-kLatitudeStepDegrees * samplingStep_);
                adjustLongitude(kLongitudeStepDegrees * samplingStep_);
                break;
            case GLFW_KEY_KP_4:
                adjustLongitude(-kLongitudeStepDegrees * samplingStep_);
                break;
            case GLFW_KEY_KP_5:
            //     resetViewPosition();
                break;
            case GLFW_KEY_KP_6:
                adjustLongitude(kLongitudeStepDegrees * samplingStep_);
                break;
            case GLFW_KEY_KP_7:
                adjustLatitude(kLatitudeStepDegrees * samplingStep_);
                adjustLongitude(-kLongitudeStepDegrees * samplingStep_);
            case GLFW_KEY_KP_8:
                adjustLatitude(kLatitudeStepDegrees * samplingStep_);
                break;
            case GLFW_KEY_KP_9:
                adjustLatitude(kLatitudeStepDegrees * samplingStep_);
                adjustLongitude(kLongitudeStepDegrees * samplingStep_);
                break;

            case GLFW_KEY_KP_ADD:
                samplingStep_ += 1;
                if (samplingStep_ > 50)
                    samplingStep_ = 50;
                needsReload_ = true;
                break;
            case GLFW_KEY_KP_SUBTRACT:
                samplingStep_ -= 1;
                if (samplingStep_ < 1)
                    samplingStep_ = 1;
                needsReload_ = true;
                break;
            default:
                break;
            }
        }
    }

    void framebufferSizeCallback(GLFWwindow* w, int width, int height) override {
        Application::framebufferSizeCallback(w, width, height);
        screenSize_.x = static_cast<float>(std::max(width, 1));
        screenSize_.y = static_cast<float>(std::max(height, 1));
    }

    void mouseCallback(GLFWwindow* w, double xpos, double ypos) override {

        const glm::vec2 mouseDelta = input->getMouseDelta(xpos, ypos);

        if (input->leftMousePressed) {
            camera->yaw -= mouseDelta.x * camera->sensitivity;
            camera->pitch -= mouseDelta.y * camera->sensitivity;
            camera->constrainPitch();
            camera->updateVectors();
        }

        if (input->rightMousePressed) {
            // rotate kLightAngleDegrees around Y axis
            float angleY = std::atan2(lightDirection_.z, lightDirection_.x);
            float angleX = std::asin(lightDirection_.y);
            angleY += glm::radians(mouseDelta.x * 0.1f);
            angleX += glm::radians(mouseDelta.y * 0.1f);

            lightDirection_.x = std::cos(angleX) * std::cos(angleY);
            lightDirection_.y = std::sin(angleX);
            lightDirection_.z = std::cos(angleX) * std::sin(angleY);
            lightDirection_ = glm::normalize(lightDirection_);
        }
    }

    void scrollCallback(GLFWwindow* w, double xoffset, double yoffset) override {
        // temporarily disabled
        // camera->distance -= static_cast<float>(yoffset) * kScrollZoomFactor;
        // camera->distance = std::clamp(camera->distance, kMinDistance, kMaxDistance);
        // camera->updateVectors();

        static double accumulator = 0.0;
        accumulator += yoffset;

        const int stepChange = static_cast<int>(std::round(accumulator));
        if (stepChange != 0) {
            samplingStep_ -= stepChange;
            if (samplingStep_ < 1)  samplingStep_ = 1;
            if (samplingStep_ > 50) samplingStep_ = 50;
            accumulator -= static_cast<double>(stepChange);
            needsReload_ = true;
        }

    }

  private:
    void loadTerrain() {
        elevationData_ =
            m_terrain->loadOrUpdateTerrain(povLatitudeDegrees_, povLongitudeDegrees_, width_, height_, samplingStep_);

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
        auto colorData = m_colorSampler->sampleColorsForTerrain(povLatitudeDegrees_, povLongitudeDegrees_, width_,
                                                                height_, totalLatSpanDegrees_, totalLonSpanDegrees_);

        mesh->vertices.clear();
        mesh->indices.clear();
        std::cout << "Generating mesh..." << std::endl;

        const float scaleZ = 1000.f / 30.325f;
        const size_t vertexCount = static_cast<size_t>(width_) * height_;

        mesh->vertices.reserve(vertexCount * 7);
        mesh->indices.reserve(static_cast<size_t>(width_ - 1) * (height_ - 1) * 6);

        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const size_t dataIndex = static_cast<size_t>(y) * width_ + x;
                const float elevation = elevationData_[dataIndex];
                const float mirroredX = static_cast<float>((width_ - 1) - x);
                const auto& color = colorData[dataIndex];

                mesh->vertices.push_back(mirroredX);
                mesh->vertices.push_back(static_cast<float>(y));
                mesh->vertices.push_back(elevation * scaleZ);
                mesh->vertices.push_back(elevation);
                mesh->vertices.push_back(color[0]);
                mesh->vertices.push_back(color[1]);
                mesh->vertices.push_back(color[2]);
            }
        }

        for (int y = 0; y < height_ - 1; ++y) {
            for (int x = 0; x < width_ - 1; ++x) {
                const unsigned int topLeft = static_cast<unsigned int>(y * width_ + x);
                const unsigned int topRight = topLeft + 1;
                const unsigned int bottomLeft = static_cast<unsigned int>((y + 1) * width_ + x);
                const unsigned int bottomRight = bottomLeft + 1;

                mesh->indices.push_back(topLeft);
                mesh->indices.push_back(bottomLeft);
                mesh->indices.push_back(topRight);

                mesh->indices.push_back(topRight);
                mesh->indices.push_back(bottomLeft);
                mesh->indices.push_back(bottomRight);
            }
        }

        std::cout << "Generated " << mesh->vertices.size() / 7 << " vertices and "
                  << mesh->indices.size() / 3 << " triangles" << std::endl;

        std::cout << "Initial elevation range: " << minElevation_ << " to " << maxElevation_ << " meters" << std::endl;
    }

    void reloadTerrain() {
        needsReload_ = false;

        std::cout << "Reloading terrain data..." << std::endl;

        auto newData =
            m_terrain->loadOrUpdateTerrain(povLatitudeDegrees_, povLongitudeDegrees_, width_, height_, samplingStep_);

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
        auto newColorData = m_colorSampler->sampleColorsForTerrain(povLatitudeDegrees_, povLongitudeDegrees_, width_,
                                                                   height_, totalLatSpanDegrees_, totalLonSpanDegrees_);

        const float scaleZ = 1000.f / 30.325f;

        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const size_t dataIndex = static_cast<size_t>(y) * width_ + x;
                const size_t vertexIndex = dataIndex * 7;

                if (vertexIndex + 6 >= mesh->vertices.size()) {
                    continue;
                }

                const float elevation = elevationData_[dataIndex];
                const auto& color = newColorData[dataIndex];

                mesh->vertices[vertexIndex + 2] = elevation * scaleZ;
                mesh->vertices[vertexIndex + 3] = elevation;
                mesh->vertices[vertexIndex + 4] = color[0];
                mesh->vertices[vertexIndex + 5] = color[1];
                mesh->vertices[vertexIndex + 6] = color[2];
            }
        }
        mesh->updateVertexData();
    }

    void centerCamera() {
        camera->target = glm::vec3(static_cast<float>(width_) / 2.0f, static_cast<float>(height_) / 2.0f, 0.0f);
        camera->distance = 600.0f;
        camera->yaw = 90.0f;
        camera->pitch = 60.0f;
        camera->updateVectors();
    }

    void adjustLatitude(double deltaDegrees) {
        const double newLat = std::clamp(povLatitudeDegrees_ + deltaDegrees, kMinLatitudeDegrees, kMaxLatitudeDegrees);
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
        std::cout << "View centered at latitude " << povLatitudeDegrees_ << " deg, longitude " << povLongitudeDegrees_
                  << " deg" << std::endl;
    }

    void updateCurvatureAmount() {
        const terrain::TileMetadata* tile = terrain::findTile(povLatitudeDegrees_, povLongitudeDegrees_);
        float degreesPerPixelLon = 45.0f / static_cast<float>(terrain::TILE_WIDTH);
        float degreesPerPixelLat = 30.0f / static_cast<float>(terrain::TILE_HEIGHT);
        if (tile) {
            const double lonSpan = terrain::longitudeSpan(*tile);
            const double latSpan = tile->maxLatitude - tile->minLatitude;
            degreesPerPixelLon = static_cast<float>(lonSpan / static_cast<double>(terrain::TILE_WIDTH));
            degreesPerPixelLat = static_cast<float>(latSpan / static_cast<double>(terrain::TILE_HEIGHT));
        }

        const float horizontalSamples = static_cast<float>(width_) * static_cast<float>(samplingStep_);
        const float verticalSamples = static_cast<float>(height_) * static_cast<float>(samplingStep_);

        const float lonSpanDegrees = horizontalSamples * degreesPerPixelLon;
        const float latSpanDegrees = verticalSamples * degreesPerPixelLat;

        const float halfWidth = static_cast<float>(width_) * 0.5f;
        const float halfHeight = static_cast<float>(height_) * 0.5f;

        float curvatureLon = 0.0f;
        if (halfWidth > 0.0f) {
            curvatureLon = glm::radians(lonSpanDegrees * 0.5f) / halfWidth;
        }

        float curvatureLat = 0.0f;
        if (halfHeight > 0.0f) {
            curvatureLat = glm::radians(latSpanDegrees * 0.5f) / halfHeight;
        }

        curvaturePerUnit_ = std::max(curvatureLon, curvatureLat);

        const float nwLat = (static_cast<float>(povLatitudeDegrees_) + latSpanDegrees * 0.5f);
        const float nwLon = (static_cast<float>(povLongitudeDegrees_) - lonSpanDegrees * 0.5f);
        const float seLat = (static_cast<float>(povLatitudeDegrees_) - latSpanDegrees * 0.5f);
        const float seLon = (static_cast<float>(povLongitudeDegrees_) + lonSpanDegrees * 0.5f);
        nwCorner = glm::vec2(nwLat, nwLon);
        seCorner = glm::vec2(seLat, seLon);

        std::cout << "NW Corner: (" << nwCorner.x << " lat, " << nwCorner.y << " lon), "
                  << "SE Corner: (" << seCorner.x << " lat, " << seCorner.y << " lon)" << std::endl;
        std::cout << "dimensions: " << width_ << "x" << height_ << std::endl;

        totalLonSpanDegrees_ = lonSpanDegrees;
        totalLatSpanDegrees_ = latSpanDegrees;
    }

    void updateOverlayStatus() {
        static std::string remember = "";
        const std::string formatted = buildStatusString();
        if (formatted != remember) {
            remember = formatted;
            std::cout << formatted << std::endl;
        }
        // fpsOverlay_.setInfoLines({formatted}); // this used to be a way to put text on the screen, never worked, TODO
    }

    std::string buildStatusString() const {
        auto formatHemisphere = [](double value, char positive, char negative) {
            const char hemi = value >= 0.0 ? positive : negative;
            const double absValue = std::abs(value);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << absValue << " deg" << hemi;
            return oss.str();
        };

        double signedLongitude = povLongitudeDegrees_;
        if (signedLongitude > 180.0) {
            signedLongitude -= 360.0;
        }

        std::ostringstream oss;
        oss << "Center " << formatHemisphere(povLatitudeDegrees_, 'N', 'S') << ' '
            << formatHemisphere(signedLongitude, 'E', 'W');
        return oss.str();
    }

    std::unique_ptr<ColorMapSampler> m_colorSampler;
    std::unique_ptr<TerrainLoader> m_terrain;
    std::string dataRoot_;
    std::vector<float> elevationData_;

    glm::vec2 screenSize_{static_cast<float>(Window::DEFAULT_WIDTH), static_cast<float>(Window::DEFAULT_HEIGHT)};
    FontOverlay fpsOverlay_;

    int width_ = 1024;
    int height_ = 1024;

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
    GLint colorModeLoc_ = -1;
    GLint lightDirLoc_ = -1;
    GLint curvatureLoc_ = -1;
    GLint meshCenterLoc_ = -1;
    float colorMode_ = 0.0f; // 0.0 for relief coloring, 1.0 for vertex color (from colormap)
    float curvaturePerUnit_ = 0.0f;
    GLint vNWCornerLoc_ = -1;
    GLint vSECornerLoc_ = -1;
    GLint dimensionsLoc_ = -1;
    glm::vec2 nwCorner = glm::vec2(0.0f);
    glm::vec2 seCorner = glm::vec2(0.0f);
    float totalLonSpanDegrees_ = 0.0f;
    float totalLatSpanDegrees_ = 0.0f;
};

int main(int argc, char** argv) {
    const char* defaultDataRoot = "./";
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