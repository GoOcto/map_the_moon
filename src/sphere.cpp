#include <GL/glew.h>

#include "application.hpp"
#include "window.hpp"
#include "shader.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>


/*

zoom -1:  each mesh is 45 x 30 degrees,  4 x 3 vertices,  3 x 2 quad
zoom  0:  each mesh is 15 x 15 degrees,  4 x 4 vertices,  3 x 3 quad
zoom  1:  each mesh is  5 x  5 degrees,  2 x 2 vertices,  1 x 1 quad
zoom  2:  each mesh is  5 x  5 degrees,  3 x 3 vertices,  2 x 2 quad
zoom  3:  each mesh is  5 x  5 degrees,  5 x 5 vertices,  4 x 4 quad
zoom  4:  each mesh is  5 x  5 degrees,  9 x 9 vertices,  8 x 8 quad
zoom  5:      ... etc ...

*/


namespace {

constexpr float kSphereRadius = 1000.0f;
constexpr int kTileLatitudeDegrees = 30;
constexpr int kTileLongitudeDegrees = 45;
constexpr int kBaseTileResolution = 2;
constexpr int kBaseSegmentsPerEdge = (kBaseTileResolution > 1) ? (kBaseTileResolution - 1) : 1;
constexpr int kMaxTileExponent = 9; // 2 * 2^9 = 1024 rows (limits LOD density)
constexpr float kMinCameraDistance = 1250.0f;
constexpr float kMaxCameraDistance = 10000.0f;
constexpr float kOrbitMinSpeedDegreesPerSecond =  2.0f;
constexpr float kOrbitMaxSpeedDegreesPerSecond = 90.0f;
constexpr float kDistanceChangePerSecond = 1500.0f;
constexpr float kPitchLimitDegrees = 89.0f;
constexpr float kOverlayUpdateInterval = 0.25f;
constexpr float kScrollMinSpeed = 60.0f;
constexpr float kScrollMaxSpeed = 1800.0f;
constexpr float kTargetTrianglePixelWidth = 10.0f;

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

const char* kOverlayVertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;

out vec3 vColor;

uniform vec2 uScreenSize;

void main() {
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
}
)";

const char* kOverlayFragmentShader = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

struct GlyphRect {
    float x;
    float y;
    float width;
    float height;
};

constexpr float kGlyphWidth = 28.0f;
constexpr float kGlyphHeight = 48.0f;
constexpr float kGlyphThickness = 6.0f;
constexpr float kGlyphMargin = 4.0f;
constexpr float kGlyphSpacing = 8.0f;
constexpr float kOverlayPadding = 18.0f;
const glm::vec3 kGlyphColor(1.0f, 1.0f, 1.0f);

const std::array<GlyphRect, 7>& SegmentRects() {
    static const std::array<GlyphRect, 7> rects = []() {
        std::array<GlyphRect, 7> r{};
        const float margin = kGlyphMargin;
        const float thickness = kGlyphThickness;
        const float width = kGlyphWidth - 2.0f * margin;
        const float halfHeight = kGlyphHeight * 0.5f;
        const float upperHeight = halfHeight - margin - thickness * 0.5f;
        const float lowerStart = halfHeight + thickness * 0.5f;
        const float lowerHeight = kGlyphHeight - margin - lowerStart;

        r[0] = {margin, margin, width, thickness};
        r[6] = {margin, halfHeight - thickness * 0.5f, width, thickness};
        r[3] = {margin, kGlyphHeight - margin - thickness, width, thickness};

        r[5] = {margin, margin, thickness, upperHeight};
        r[1] = {kGlyphWidth - margin - thickness, margin, thickness, upperHeight};
        r[4] = {margin, lowerStart, thickness, lowerHeight};
        r[2] = {kGlyphWidth - margin - thickness, lowerStart, thickness, lowerHeight};
        return r;
    }();
    return rects;
}

void AppendRect(float originX,
                float originY,
                const GlyphRect& rect,
                const glm::vec3& color,
                std::vector<float>& buffer) {
    const float x0 = originX + rect.x;
    const float y0 = originY + rect.y;
    const float x1 = x0 + rect.width;
    const float y1 = y0 + rect.height;

    auto pushVertex = [&buffer, &color](float x, float y) {
        buffer.push_back(x);
        buffer.push_back(y);
        buffer.push_back(color.r);
        buffer.push_back(color.g);
        buffer.push_back(color.b);
    };

    pushVertex(x0, y0);
    pushVertex(x1, y0);
    pushVertex(x1, y1);

    pushVertex(x0, y0);
    pushVertex(x1, y1);
    pushVertex(x0, y1);
}

float AppendDigit(int digit,
                  float originX,
                  float originY,
                  const glm::vec3& color,
                  std::vector<float>& buffer) {
    static const std::array<uint8_t, 10> kDigitMasks = {
        0x3F, // 0
        0x06, // 1
        0x5B, // 2
        0x4F, // 3
        0x66, // 4
        0x6D, // 5
        0x7D, // 6
        0x07, // 7
        0x7F, // 8
        0x6F  // 9
    };

    const auto& segments = SegmentRects();
    const uint8_t mask = kDigitMasks[std::clamp(digit, 0, 9)];
    for (int i = 0; i < 7; ++i) {
        if (mask & (1 << i)) {
            AppendRect(originX, originY, segments[i], color, buffer);
        }
    }

    return kGlyphWidth + kGlyphSpacing;
}

float AppendLetterF(float originX,
                    float originY,
                    const glm::vec3& color,
                    std::vector<float>& buffer) {
    const float margin = kGlyphMargin;
    const float thickness = kGlyphThickness;
    AppendRect(originX, originY, {margin, margin, kGlyphWidth - 2.0f * margin, thickness}, color, buffer);
    AppendRect(originX, originY, {margin, kGlyphHeight * 0.5f - thickness * 0.5f, kGlyphWidth * 0.6f, thickness}, color, buffer);
    AppendRect(originX, originY, {margin, margin, thickness, kGlyphHeight - 2.0f * margin}, color, buffer);
    return kGlyphWidth + kGlyphSpacing;
}

float AppendLetterP(float originX,
                    float originY,
                    const glm::vec3& color,
                    std::vector<float>& buffer) {
    const float margin = kGlyphMargin;
    const float thickness = kGlyphThickness;
    const float halfHeight = kGlyphHeight * 0.5f;
    AppendRect(originX, originY, {margin, margin, kGlyphWidth - 2.0f * margin, thickness}, color, buffer);
    AppendRect(originX, originY, {margin, halfHeight - thickness * 0.5f, kGlyphWidth - 2.0f * margin, thickness}, color, buffer);
    AppendRect(originX, originY, {kGlyphWidth - margin - thickness, margin, thickness, halfHeight - margin}, color, buffer);
    AppendRect(originX, originY, {margin, margin, thickness, kGlyphHeight - 2.0f * margin}, color, buffer);
    return kGlyphWidth + kGlyphSpacing;
}

float AppendLetterS(float originX,
                    float originY,
                    const glm::vec3& color,
                    std::vector<float>& buffer) {
    return AppendDigit(5, originX, originY, color, buffer);
}

float AppendColon(float originX,
                  float originY,
                  const glm::vec3& color,
                  std::vector<float>& buffer) {
    const float dotSize = kGlyphThickness;
    const float localX = kGlyphWidth * 0.5f - dotSize * 0.5f;
    const float topLocalY = kGlyphHeight * 0.3f - dotSize * 0.5f;
    const float bottomLocalY = kGlyphHeight * 0.7f - dotSize * 0.5f;
    AppendRect(originX, originY, {localX, topLocalY, dotSize, dotSize}, color, buffer);
    AppendRect(originX, originY, {localX, bottomLocalY, dotSize, dotSize}, color, buffer);
    return kGlyphWidth * 0.5f + kGlyphSpacing;
}

float AppendSpace(float /*originX*/,
                  float /*originY*/,
                  const glm::vec3& /*color*/,
                  std::vector<float>& /*buffer*/) {
    return kGlyphWidth * 0.5f;
}

float AppendGlyph(char ch,
                  float originX,
                  float originY,
                  const glm::vec3& color,
                  std::vector<float>& buffer) {
    if (ch >= '0' && ch <= '9') {
        return AppendDigit(ch - '0', originX, originY, color, buffer);
    }

    switch (ch) {
        case 'F':
        case 'f':
            return AppendLetterF(originX, originY, color, buffer);
        case 'P':
        case 'p':
            return AppendLetterP(originX, originY, color, buffer);
        case 'S':
        case 's':
            return AppendLetterS(originX, originY, color, buffer);
        case ':':
            return AppendColon(originX, originY, color, buffer);
        case ' ':
            return AppendSpace(originX, originY, color, buffer);
        default:
            return AppendSpace(originX, originY, color, buffer);
    }
}

void BuildOverlayText(const std::string& text,
                      float originX,
                      float originY,
                      const glm::vec3& color,
                      std::vector<float>& buffer) {
    float cursor = originX;
    for (char ch : text) {
        cursor += AppendGlyph(ch, cursor, originY, color, buffer);
    }
}

struct Tile {
    float latStartDeg = 0.0f;
    float lonStartDeg = 0.0f;
    float latCenterRad = 0.0f;
    glm::vec3 color{1.0f};
    glm::vec3 centerDirection{0.0f, 0.0f, 1.0f};
    glm::vec3 centerPosition{0.0f, 0.0f, kSphereRadius};
    float widthWorld = 0.0f;
    float heightWorld = 0.0f;
    float maxWorldSpan = 0.0f;
    int currentExponent = -1;
    bool visible = true;
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    size_t vertexCount = 0;
};

glm::vec3 SphericalToCartesian(float radius, float latitudeRad, float longitudeRad) {
    const float cosLat = std::cos(latitudeRad);
    const float sinLat = std::sin(latitudeRad);
    const float cosLon = std::cos(longitudeRad);
    const float sinLon = std::sin(longitudeRad);

    return glm::vec3(radius * cosLat * cosLon,
                     radius * cosLat * sinLon,
                     radius * sinLat);
}

int DetermineExponentForTargetSegments(int targetSegments) {
    targetSegments = std::max(targetSegments, 1);
    for (int exponent = 0; exponent <= kMaxTileExponent; ++exponent) {
        const int segments = std::max(1, kBaseSegmentsPerEdge * (1 << exponent));
        if (segments >= targetSegments) {
            return exponent;
        }
    }
    return kMaxTileExponent;
}

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

    ~SphereViewerApp() override {
        if (overlayVbo_ != 0) {
            glDeleteBuffers(1, &overlayVbo_);
        }
        if (overlayVao_ != 0) {
            glDeleteVertexArrays(1, &overlayVao_);
        }
    }

protected:
    void setup() override {
        setupCallbacks();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        shader = std::make_unique<ShaderProgram>(kVertexShaderSource, kFragmentShaderSource);

        initializeTiles();
        meshDirty_ = true;
        updateTileLODs(true);

        shader->use();
        modelLoc_ = shader->getUniformLocation("model");
        viewLoc_ = shader->getUniformLocation("view");
        projectionLoc_ = shader->getUniformLocation("projection");
        lightDirLoc_ = shader->getUniformLocation("lightDirection");
        cameraPosLoc_ = shader->getUniformLocation("cameraPosition");

        overlayShader_ = std::make_unique<ShaderProgram>(kOverlayVertexShader, kOverlayFragmentShader);
        glGenVertexArrays(1, &overlayVao_);
        glGenBuffers(1, &overlayVbo_);
        glBindVertexArray(overlayVao_);
        glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        overlayScreenSizeLoc_ = overlayShader_->getUniformLocation("uScreenSize");

        fpsText_ = "FPS 0";
        rebuildOverlayGeometry();
    }

    void update(float deltaTime) override {
        handleCameraInput(deltaTime);
        updateTileLODs();
        updateFpsCounter(deltaTime);
    }

    void render() override {
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

        if (mesh && mesh->getIndexCount() > 0) {
            mesh->draw();
        }

        renderOverlay();
    }

    void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods) override {
        if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
            wireframeEnabled_ = !wireframeEnabled_;
            wireframeMode = wireframeEnabled_;
            glPolygonMode(GL_FRONT_AND_BACK, wireframeEnabled_ ? GL_LINE : GL_FILL);
            input->handleKeyPress(key);
            return;
        }

        if (key == GLFW_KEY_R) {
            if (action == GLFW_PRESS) {
                input->handleKeyPress(key);
            } else if (action == GLFW_RELEASE) {
                input->handleKeyRelease(key);
            }
            return;
        }

        Application::keyCallback(w, key, scancode, action, mods);
    }

    void framebufferSizeCallback(GLFWwindow* w, int width, int height) override {
        Application::framebufferSizeCallback(w, width, height);
        screenSize_.x = static_cast<float>(std::max(width, 1));
        screenSize_.y = static_cast<float>(std::max(height, 1));
        rebuildOverlayGeometry();
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
            // Scale mouse-drag orbiting with the same distance-aware speed applied to keyboard input.
            const float yawDelta = mouseDelta.x * camera->sensitivity * orbitSpeed * deltaTime;
            const float pitchDelta = mouseDelta.y * camera->sensitivity * orbitSpeed * deltaTime;
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
        std::cout << "(FPS counter shown top-left)" << std::endl;
        std::cout << "==============================\n" << std::endl;
    }

private:
    void initializeTiles() {
        tiles_.clear();
        std::mt19937 rng(123456u);
        std::uniform_real_distribution<float> colorDist(0.25f, 0.95f);

        const int latTileCount = 180 / kTileLatitudeDegrees;
        const int lonTileCount = 360 / kTileLongitudeDegrees;
        const float latSpanRad = glm::radians(static_cast<float>(kTileLatitudeDegrees));
        const float lonSpanRad = glm::radians(static_cast<float>(kTileLongitudeDegrees));

        tiles_.reserve(static_cast<size_t>(latTileCount * lonTileCount));

        for (int latIdx = 0; latIdx < latTileCount; ++latIdx) {
            const float latStart = -90.0f + static_cast<float>(latIdx * kTileLatitudeDegrees);
            const float latCenter = latStart + 0.5f * kTileLatitudeDegrees;

            for (int lonIdx = 0; lonIdx < lonTileCount; ++lonIdx) {
                const float lonStart = -180.0f + static_cast<float>(lonIdx * kTileLongitudeDegrees);
                const float lonCenter = lonStart + 0.5f * kTileLongitudeDegrees;

                Tile tile;
                tile.latStartDeg = latStart;
                tile.lonStartDeg = lonStart;
                tile.latCenterRad = glm::radians(latCenter);
                tile.color = glm::vec3(colorDist(rng), colorDist(rng), colorDist(rng));
                tile.centerDirection = glm::normalize(
                    SphericalToCartesian(1.0f, glm::radians(latCenter), glm::radians(lonCenter)));
                tile.centerPosition = tile.centerDirection * kSphereRadius;

                const float cosLat = std::abs(std::cos(tile.latCenterRad));
                tile.widthWorld = kSphereRadius * lonSpanRad * std::max(0.001f, cosLat);
                tile.heightWorld = kSphereRadius * latSpanRad;
                tile.maxWorldSpan = std::max(tile.widthWorld, tile.heightWorld);

                tiles_.push_back(std::move(tile));
            }
        }
    }

    void handleCameraInput(float deltaTime) {
        bool updated = false;
        const float orbitSpeed = computeOrbitSpeed();

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
            camera->distance = std::min(camera->distance + kDistanceChangePerSecond * deltaTime, kMaxCameraDistance);
            updated = true;
        }
        if (input->isKeyPressed(GLFW_KEY_F)) {
            camera->distance = std::max(camera->distance - kDistanceChangePerSecond * deltaTime, kMinCameraDistance);
            updated = true;
        }

        if (updated) {
            camera->updateVectors();
        }
    }

    void updateTileLODs(bool force = false) {
        if (tiles_.empty()) {
            return;
        }

        const float screenWidth = std::max(screenSize_.x, 1.0f);
        const float screenHeight = std::max(screenSize_.y, 1.0f);
        const float aspectRatio = screenWidth / screenHeight;
        float fovYRad = glm::radians(std::clamp(camera->fov, 1.0f, 179.0f));
        float fovXRad = 2.0f * std::atan(std::tan(fovYRad * 0.5f) * aspectRatio);
        if (!std::isfinite(fovXRad) || fovXRad <= 0.0f) {
            fovXRad = fovYRad;
        }

        const glm::vec3 cameraPos = camera->position;
        const glm::vec3 cameraForward = glm::normalize(camera->front);
        const int maxSegments = std::max(1, kBaseTileResolution * (1 << kMaxTileExponent));
        const glm::mat4 viewMatrix = getViewMatrix();
        const glm::mat4 projectionMatrix = getProjectionMatrix();
        const glm::mat4 viewProjection = projectionMatrix * viewMatrix;
        const float clipTolerance = 1.05f;

        bool anyChanged = false;

        for (auto& tile : tiles_) {
            const glm::vec3 toTile = tile.centerPosition - cameraPos;
            float distance = glm::length(toTile);
            if (!std::isfinite(distance) || distance <= 1e-3f) {
                distance = 1e-3f;
            }

            const glm::vec3 toTileDir = toTile / distance;
            const float facing = glm::dot(toTileDir, cameraForward);
            const glm::vec3 toCameraDir = glm::normalize(cameraPos - tile.centerPosition);
            const float normalFacing = glm::dot(tile.centerDirection, toCameraDir);

            const glm::vec4 clipPos = viewProjection * glm::vec4(tile.centerPosition, 1.0f);
            bool insideFrustum = true;
            if (clipPos.w <= 0.0f) {
                insideFrustum = false;
            } else {
                const float invW = 1.0f / clipPos.w;
                const float ndcX = clipPos.x * invW;
                const float ndcY = clipPos.y * invW;
                const float ndcZ = clipPos.z * invW;
                if (std::abs(ndcX) > clipTolerance || std::abs(ndcY) > clipTolerance || ndcZ < -clipTolerance || ndcZ > clipTolerance) {
                    insideFrustum = false;
                }
            }

            const bool tileVisible = facing > 0.0f && normalFacing > 0.0f && insideFrustum;
            if (tile.visible != tileVisible) {
                tile.visible = tileVisible;
                anyChanged = true;
            }

            if (!tile.visible) {
                continue;
            }

            int targetExponent = 0;
            if (tile.maxWorldSpan > 0.0f) {
                const float facingFactor = std::max(0.0f, facing);
                const float projectedSpan = tile.maxWorldSpan * facingFactor;
                float angularWidth = 2.0f * std::atan(projectedSpan * 0.5f / distance);
                angularWidth = std::max(0.0f, angularWidth);

                float apparentPixelWidth = (angularWidth / fovXRad) * screenWidth;
                if (!std::isfinite(apparentPixelWidth) || apparentPixelWidth < 0.0f) {
                    apparentPixelWidth = 0.0f;
                }

                int targetSegments = static_cast<int>(std::ceil(apparentPixelWidth / kTargetTrianglePixelWidth));
                targetSegments = std::clamp(targetSegments, 1, maxSegments);
                targetExponent = DetermineExponentForTargetSegments(targetSegments);
            }

            if (force || tile.currentExponent != targetExponent || tile.vertexCount == 0) {
                generateTileGeometry(tile, targetExponent);
                tile.currentExponent = targetExponent;
                anyChanged = true;
            }
        }

        if (anyChanged || force || meshDirty_) {
            rebuildMesh();
            meshDirty_ = false;
        }
    }

    void generateTileGeometry(Tile& tile, int exponent) {
        const int subdivisions = (1 << exponent); // 1, 2, 4, 8, ...
        const int maxSubdivisions = 1 << kMaxTileExponent;
        const int rows = std::clamp(kBaseTileResolution * subdivisions,
                                    kBaseTileResolution,
                                    kBaseTileResolution * maxSubdivisions);
        const int cols = rows;

        const float latSpanRad = glm::radians(static_cast<float>(kTileLatitudeDegrees));
        const float lonSpanRad = glm::radians(static_cast<float>(kTileLongitudeDegrees));
        const float latStartRad = glm::radians(tile.latStartDeg);
#if 0
        const float thisLatStartRad = latStartRad + 0.0f;
#else
        const float thisLatStartRad = latStartRad;
#endif
        const float lonStartRad = glm::radians(tile.lonStartDeg);

        const float latStep = latSpanRad / static_cast<float>(rows - 1);
        const float lonStep = lonSpanRad / static_cast<float>(cols - 1);

        tile.vertices.clear();
        tile.indices.clear();
        tile.vertices.reserve(static_cast<size_t>(rows * cols * 9));
        tile.indices.reserve(static_cast<size_t>((rows - 1) * (cols - 1) * 6));

        for (int r = 0; r < rows; ++r) {
            const float lat = thisLatStartRad + static_cast<float>(r) * latStep;
            for (int c = 0; c < cols; ++c) {
                const float lon = lonStartRad + static_cast<float>(c) * lonStep;
                const glm::vec3 pos = SphericalToCartesian(kSphereRadius, lat, lon);
                const glm::vec3 norm = glm::normalize(pos);

                tile.vertices.push_back(pos.x);
                tile.vertices.push_back(pos.y);
                tile.vertices.push_back(pos.z);

                tile.vertices.push_back(norm.x);
                tile.vertices.push_back(norm.y);
                tile.vertices.push_back(norm.z);

                tile.vertices.push_back(tile.color.r);
                tile.vertices.push_back(tile.color.g);
                tile.vertices.push_back(tile.color.b);
            }
        }

        for (int r = 0; r < rows - 1; ++r) {
            for (int c = 0; c < cols - 1; ++c) {
                const unsigned int current = static_cast<unsigned int>(r * cols + c);
                const unsigned int nextRow = static_cast<unsigned int>((r + 1) * cols + c);

                tile.indices.push_back(current);
                tile.indices.push_back(nextRow);
                tile.indices.push_back(current + 1);

                tile.indices.push_back(current + 1);
                tile.indices.push_back(nextRow);
                tile.indices.push_back(nextRow + 1);
            }
        }

        tile.vertexCount = static_cast<size_t>(rows * cols);
    }

    void rebuildMesh() {
        if (!mesh) {
            return;
        }

        mesh->vertices.clear();
        mesh->indices.clear();

        size_t vertexOffset = 0;
        for (const auto& tile : tiles_) {
            if (!tile.visible || tile.vertexCount == 0) {
                continue;
            }
            mesh->vertices.insert(mesh->vertices.end(), tile.vertices.begin(), tile.vertices.end());
            for (unsigned int index : tile.indices) {
                mesh->indices.push_back(static_cast<unsigned int>(index + vertexOffset));
            }
            vertexOffset += tile.vertexCount;
        }

        mesh->uploadData();

        if (!attributesConfigured_) {
            mesh->setupVertexAttributes({3, 3, 3});
            attributesConfigured_ = true;
        }
    }

    void updateFpsCounter(float deltaTime) {
        fpsAccumulator_ += deltaTime;
        ++frameCount_;

        if (fpsAccumulator_ >= kOverlayUpdateInterval) {
            currentFps_ = (fpsAccumulator_ > 0.0f) ? static_cast<float>(frameCount_) / fpsAccumulator_ : 0.0f;
            frameCount_ = 0;
            fpsAccumulator_ = 0.0f;

            std::ostringstream oss;
            oss << "FPS " << std::fixed << std::setprecision(0) << currentFps_;
            fpsText_ = oss.str();
            rebuildOverlayGeometry();
        }
    }

    void rebuildOverlayGeometry() {
        if (overlayVbo_ == 0) {
            return;
        }

        overlayVertices_.clear();
        BuildOverlayText(fpsText_, kOverlayPadding, kOverlayPadding, kGlyphColor, overlayVertices_);
        overlayVertexCount_ = overlayVertices_.size() / 5;

        glBindVertexArray(overlayVao_);
        glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     overlayVertices_.size() * sizeof(float),
                     overlayVertices_.data(),
                     GL_DYNAMIC_DRAW);
        glBindVertexArray(0);
    }

    void renderOverlay() {
        if (!overlayShader_ || overlayVertexCount_ == 0) {
            return;
        }

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        overlayShader_->use();
        glUniform2f(overlayScreenSizeLoc_, screenSize_.x, screenSize_.y);
        glBindVertexArray(overlayVao_);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(overlayVertexCount_));
        glBindVertexArray(0);

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    std::vector<Tile> tiles_;
    bool meshDirty_ = false;
    bool attributesConfigured_ = false;
    bool wireframeEnabled_ = false;

    std::unique_ptr<ShaderProgram> overlayShader_;
    std::vector<float> overlayVertices_;
    size_t overlayVertexCount_ = 0;
    GLuint overlayVao_ = 0;
    GLuint overlayVbo_ = 0;
    GLint overlayScreenSizeLoc_ = -1;
    glm::vec2 screenSize_{static_cast<float>(Window::DEFAULT_WIDTH), static_cast<float>(Window::DEFAULT_HEIGHT)};

    float fpsAccumulator_ = 0.0f;
    int frameCount_ = 0;
    float currentFps_ = 0.0f;
    std::string fpsText_ = "FPS 0";

    GLint modelLoc_ = -1;
    GLint viewLoc_ = -1;
    GLint projectionLoc_ = -1;
    GLint lightDirLoc_ = -1;
    GLint cameraPosLoc_ = -1;

    float computeScrollZoomSpeed() const {
        const float range = std::max(kMaxCameraDistance - kMinCameraDistance, 1.0f);
        float ratio = (camera->distance - kMinCameraDistance) / range;
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        const float eased = ratio * ratio * ratio;  // smooth ease-out as distance grows
        return kScrollMinSpeed + (kScrollMaxSpeed - kScrollMinSpeed) * eased;
    }

    float computeOrbitSpeed() const {
        const float range = std::max(kMaxCameraDistance - kMinCameraDistance, 1.0f);
        float ratio = (camera->distance - kMinCameraDistance) / range;
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        const float eased = powf(ratio, 0.707f);
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
