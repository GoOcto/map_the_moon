#include <GL/glew.h>

#include "application.hpp"
#include "window.hpp"
#include "shader.hpp"

// FONT RENDERING INCLUDES
// The stb_truetype library does the heavy lifting of font processing.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
// This header contains the font data itself as a C-style byte array.
// #include "font_proggy_clean.h"

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
#include <fstream>


namespace {

// --- CONSTANTS ---
constexpr float kSphereRadius = 1000.0f;
constexpr int kTileLatitudeDegrees = 5;
constexpr int kTileLongitudeDegrees = 5;
constexpr int kBaseTileResolution = 2;
constexpr int kBaseSegmentsPerEdge = (kBaseTileResolution > 1) ? (kBaseTileResolution - 1) : 1;
constexpr int kMaxTileExponent = 9;
constexpr float kMinCameraDistance = 1100.0f;
constexpr float kMaxCameraDistance = 10000.0f;
constexpr float kOrbitMinSpeedDegreesPerSecond =  2.0f;
constexpr float kOrbitMaxSpeedDegreesPerSecond = 90.0f;
constexpr float kDistanceChangePerSecond = 1500.0f;
constexpr float kPitchLimitDegrees = 89.0f;
constexpr float kScrollMinSpeed = 60.0f;
constexpr float kScrollMaxSpeed = 1800.0f;
constexpr float kTargetTrianglePixelWidth = 100.0f;

// --- FONT CONSTANTS ---
constexpr float kOverlayUpdateInterval = 0.25f;
constexpr float kOverlayPadding = 10.0f;
constexpr float kFontSize = 15.0f; // Font size in pixels
constexpr int kFontAtlasWidth = 512;
constexpr int kFontAtlasHeight = 512;

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

// --- OVERLAY SHADERS ---
// These shaders are designed to render textured quads for the text.

const char* kOverlayVertexShader = R"(
#version 330 core
// Input vertex now contains position (X, Y) and texture coordinates (U, V)
layout (location = 0) in vec4 aVertex; 
out vec2 vTexCoord;

// We use a proper orthographic projection for pixel-perfect 2D rendering.
uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * vec4(aVertex.xy, 0.0, 1.0);
    vTexCoord = aVertex.zw;
}
)";

const char* kOverlayFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;

// The font atlas is passed as a sampler.
uniform sampler2D uText;
// The text color can be changed dynamically.
uniform vec3 uTextColor;

void main() {
    // The texture's 'r' channel contains the glyph's alpha.
    float alpha = texture(uText, vTexCoord).r;
    // The final color is the text color, with transparency from the font atlas.
    FragColor = vec4(uTextColor, alpha);
}
)";


// NOTE: All the old glyph drawing functions (AppendDigit, AppendRect, etc.) have been removed.

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
        if (overlayVbo_ != 0) glDeleteBuffers(1, &overlayVbo_);
        if (overlayVao_ != 0) glDeleteVertexArrays(1, &overlayVao_);
        if (fontTexture_ != 0) glDeleteTextures(1, &fontTexture_);
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

        // --- FONT INITIALIZATION ---
        setupFontRendering();

        fpsText_ = "FPS 0";
        rebuildOverlayGeometry();
    }

    void update(float deltaTime) override {
        handleCameraInput(deltaTime);
        updateTileLODs();
        updateFpsCounter(deltaTime);
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
        Application::keyCallback(w, key, scancode, action, mods);
    }

    void framebufferSizeCallback(GLFWwindow* w, int width, int height) override {
        Application::framebufferSizeCallback(w, width, height);
        screenSize_.x = static_cast<float>(std::max(width, 1));
        screenSize_.y = static_cast<float>(std::max(height, 1));
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
            const float yawDelta = mouseDelta.x * camera->sensitivity * orbitSpeed * 0.1f;
            const float pitchDelta = mouseDelta.y * camera->sensitivity * orbitSpeed * 0.1f;
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
        const float maxTileAngularSpan = glm::radians(std::max(kTileLatitudeDegrees, kTileLongitudeDegrees) * 0.5f);
        const float normalCullThreshold = -std::sin(maxTileAngularSpan);

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

            const bool tileVisible = facing > 0.0f && 
                         normalFacing > normalCullThreshold;

            if (tile.visible != tileVisible) {
                tile.visible = tileVisible;
                anyChanged = true;
            }

            if (!tile.visible) continue;

            int targetExponent = 0;
            if (tile.maxWorldSpan > 0.0f) {
                const float projectedSpan = tile.maxWorldSpan * std::max(0.0f, facing);
                float angularWidth = 2.0f * std::atan(projectedSpan * 0.5f / distance);
                angularWidth = std::max(0.0f, angularWidth);
                float apparentPixelWidth = (angularWidth / fovXRad) * screenWidth;
                apparentPixelWidth = std::max(0.0f, apparentPixelWidth);
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
        const int subdivisions = (1 << exponent);
        const int maxSubdivisions = 1 << kMaxTileExponent;
        const int rows = std::clamp(kBaseTileResolution * subdivisions, kBaseTileResolution, kBaseTileResolution * maxSubdivisions);
        const int cols = rows;

        const float latSpanRad = glm::radians(static_cast<float>(kTileLatitudeDegrees));
        const float lonSpanRad = glm::radians(static_cast<float>(kTileLongitudeDegrees));
        const float latStartRad = glm::radians(tile.latStartDeg);
        const float lonStartRad = glm::radians(tile.lonStartDeg);

        const float latStep = latSpanRad / static_cast<float>(rows - 1);
        const float lonStep = lonSpanRad / static_cast<float>(cols - 1);

        tile.vertices.assign(static_cast<size_t>(rows * cols * 9), 0.0f);
        tile.indices.clear();
        tile.indices.reserve(static_cast<size_t>((rows - 1) * (cols - 1) * 6));
        
        for (int r = 0; r < rows; ++r) {
            const float lat = latStartRad + static_cast<float>(r) * latStep;
            for (int c = 0; c < cols; ++c) {
                const float lon = lonStartRad + static_cast<float>(c) * lonStep;
                const glm::vec3 pos = SphericalToCartesian(kSphereRadius, lat, lon);
                const glm::vec3 norm = glm::normalize(pos);
                
                size_t baseIdx = (r * cols + c) * 9;
                tile.vertices[baseIdx + 0] = pos.x;
                tile.vertices[baseIdx + 1] = pos.y;
                tile.vertices[baseIdx + 2] = pos.z;
                tile.vertices[baseIdx + 3] = norm.x;
                tile.vertices[baseIdx + 4] = norm.y;
                tile.vertices[baseIdx + 5] = norm.z;
                tile.vertices[baseIdx + 6] = tile.color.r;
                tile.vertices[baseIdx + 7] = tile.color.g;
                tile.vertices[baseIdx + 8] = tile.color.b;
            }
        }

        for (int r = 0; r < rows - 1; ++r) {
            for (int c = 0; c < cols - 1; ++c) {
                const unsigned int current = static_cast<unsigned int>(r * cols + c);
                const unsigned int nextRow = static_cast<unsigned int>((r + 1) * cols + c);

                tile.indices.push_back(current);
                tile.indices.push_back(current + 1);
                tile.indices.push_back(nextRow);

                tile.indices.push_back(nextRow);
                tile.indices.push_back(current + 1);
                tile.indices.push_back(nextRow + 1);
            }
        }
        tile.vertexCount = static_cast<size_t>(rows * cols);
    }

    void rebuildMesh() {
        if (!mesh) return;
        mesh->vertices.clear();
        mesh->indices.clear();

        size_t vertexOffset = 0;
        for (const auto& tile : tiles_) {
            if (!tile.visible || tile.vertexCount == 0) continue;
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
    
    void setupFontRendering() {
        // --- FONT INITIALIZATION ---
        // 1. Load the font file from disk
        std::ifstream fontFile("fonts/ProggyClean.ttf", std::ios::binary | std::ios::ate);
        if (!fontFile.is_open()) {
            throw std::runtime_error("Failed to open font file: fonts/ProggyClean.ttf");
        }

        std::streamsize size = fontFile.tellg();
        fontFile.seekg(0, std::ios::beg);

        std::vector<char> fontBuffer(size);
        if (!fontFile.read(fontBuffer.data(), size)) {
            throw std::runtime_error("Failed to read font file.");
        }
        fontFile.close();

        // 2. Create OpenGL texture for the font atlas
        glGenTextures(1, &fontTexture_);
        glBindTexture(GL_TEXTURE_2D, fontTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // 3. Create a temporary bitmap in memory to hold the rasterized font
        std::vector<unsigned char> fontBitmap(kFontAtlasWidth * kFontAtlasHeight);

        // 4. Use stb_truetype to "bake" character glyphs from the loaded font file into our bitmap
        stbtt_BakeFontBitmap(reinterpret_cast<const unsigned char*>(fontBuffer.data()), 0, kFontSize, fontBitmap.data(),
                             kFontAtlasWidth, kFontAtlasHeight, 32, 96, cdata_);

        // 5. Upload the bitmap to the GPU texture. We use GL_RED as it's a single-channel image.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, kFontAtlasWidth, kFontAtlasHeight, 0,
                     GL_RED, GL_UNSIGNED_BYTE, fontBitmap.data());

        // 6. Setup shader and get uniform locations
        overlayShader_ = std::make_unique<ShaderProgram>(kOverlayVertexShader, kOverlayFragmentShader);
        overlayProjectionLoc_ = overlayShader_->getUniformLocation("uProjection");
        overlayTextColorLoc_  = overlayShader_->getUniformLocation("uTextColor");
        overlayTextureLoc_    = overlayShader_->getUniformLocation("uText");

        // 7. Setup VAO and VBO for drawing the text quads
        glGenVertexArrays(1, &overlayVao_);
        glGenBuffers(1, &overlayVbo_);
        glBindVertexArray(overlayVao_);
        glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4 * 64, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void rebuildOverlayGeometry() {
        if (!overlayShader_) return;
        
        std::vector<float> vertices;
        vertices.reserve(fpsText_.length() * 6 * 4); // 6 vertices per char, 4 floats per vertex

        // Start cursor position
        float x = kOverlayPadding;
        float y = kOverlayPadding; 

        for (char c : fpsText_) {
            if (c >= 32 && c < 128) {
                stbtt_aligned_quad q;
                // This function calculates the vertex positions for the character's quad
                stbtt_GetBakedQuad(cdata_, kFontAtlasWidth, kFontAtlasHeight, c - 32, &x, &y, &q, 1);
                
                // Adjust y positions because stb_truetype's origin is different from OpenGL's
                const float y0 = q.y0 + kFontSize + 5;
                const float y1 = q.y1 + kFontSize + 5;

                // Build the 6 vertices for the character's textured quad
                float quad_verts[] = {
                    q.x0, y1, q.s0, q.t1,
                    q.x0, y0, q.s0, q.t0,
                    q.x1, y0, q.s1, q.t0,

                    q.x0, y1, q.s0, q.t1,
                    q.x1, y0, q.s1, q.t0,
                    q.x1, y1, q.s1, q.t1,
                };
                vertices.insert(vertices.end(), std::begin(quad_verts), std::end(quad_verts));
            }
        }
        overlayVertexCount_ = vertices.size() / 4;

        glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void renderOverlay() {
        if (!overlayShader_ || overlayVertexCount_ == 0) {
            return;
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);        
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        overlayShader_->use();

        glm::mat4 projection = glm::ortho(0.0f, screenSize_.x, screenSize_.y, 0.0f);
        glUniformMatrix4fv(overlayProjectionLoc_, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(overlayTextColorLoc_, 1.0f, 1.0f, 1.0f);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fontTexture_);
        glUniform1i(overlayTextureLoc_, 0);

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

    // --- FONT RENDERING ---
    std::unique_ptr<ShaderProgram> overlayShader_;
    GLuint fontTexture_ = 0;
    stbtt_bakedchar cdata_[96]; // Character data for 96 ASCII characters
    GLuint overlayVao_ = 0;
    GLuint overlayVbo_ = 0;
    GLint overlayProjectionLoc_ = -1;
    GLint overlayTextColorLoc_ = -1;
    GLint overlayTextureLoc_ = -1;
    size_t overlayVertexCount_ = 0;
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

