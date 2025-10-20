#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <cmath>

#include "mesh.hpp"
#include "camera.hpp"
#include "shader.hpp"

namespace {
    // --- CONSTANTS ---
    constexpr float kDefaultSphereRadius = 1000.0f;
    constexpr int kTileLatitudeDegrees = 1;
    constexpr int kTileLongitudeDegrees = 1;
    constexpr int kBaseTileResolution = 2;
    constexpr int kBaseSegmentsPerEdge = (kBaseTileResolution > 1) ? (kBaseTileResolution - 1) : 1;
    constexpr int kMaxTileExponent = 9; // Up to 512 segments per edge
    constexpr float kTargetTrianglePixelWidth = 16.0f;

    struct Tile {
        float latStartDeg = 0.0f;
        float lonStartDeg = 0.0f;
        float latCenterRad = 0.0f;
        glm::vec3 color{1.0f};
        glm::vec3 centerDirection{0.0f, 0.0f, 1.0f};
        glm::vec3 centerPosition{0.0f, 0.0f, kDefaultSphereRadius};
        float widthWorld = 0.0f;
        float heightWorld = 0.0f;
        float maxWorldSpan = 0.0f;
        int currentExponent = -1;
        bool visible = true;
        std::vector<float> vertices;
        std::vector<unsigned int> indices;
        size_t vertexCount = 0;
    };

    inline glm::vec3 SphericalToCartesian(float radius, float latitudeRad, float longitudeRad) {
        const float cosLat = std::cos(latitudeRad);
        const float sinLat = std::sin(latitudeRad);
        const float cosLon = std::cos(longitudeRad);
        const float sinLon = std::sin(longitudeRad);

        return glm::vec3(radius * cosLat * cosLon,
                         radius * cosLat * sinLon,
                         radius * sinLat);
    }

    inline int DetermineExponentForTargetSegments(int targetSegments) {
        targetSegments = std::max(targetSegments, 1);
        for (int exponent = 0; exponent <= kMaxTileExponent; ++exponent) {
            const int segments = std::max(1, kBaseSegmentsPerEdge * (1 << exponent));
            if (segments >= targetSegments) {
                return exponent;
            }
        }
        return kMaxTileExponent;
    }
}

class Sphere {
public:
    Sphere(float radius = kDefaultSphereRadius) : radius_(radius) {
        mesh_ = std::make_unique<Mesh>();
        initializeTiles();
        updateLODs(nullptr, {0,0}, true);
    }

    void updateLODs(const Camera* camera, const glm::vec2& screenSize, bool force = false) {
        if (tiles_.empty()) {
            return;
        }

        bool anyChanged = false;

        if (camera) {
            const float screenWidth = std::max(screenSize.x, 1.0f);
            const float screenHeight = std::max(screenSize.y, 1.0f);
            const float aspectRatio = screenWidth / screenHeight;
            float fovYRad = glm::radians(std::clamp(camera->fov, 1.0f, 179.0f));
            float fovXRad = 2.0f * std::atan(std::tan(fovYRad * 0.5f) * aspectRatio);
            if (!std::isfinite(fovXRad) || fovXRad <= 0.0f) {
                fovXRad = fovYRad;
            }

            const glm::vec3 cameraPos = camera->position;
            const glm::vec3 cameraForward = glm::normalize(camera->front);
            const int maxSegments = std::max(1, kBaseTileResolution * (1 << kMaxTileExponent));
            
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

                const bool tileVisible = facing > 0.0f && normalFacing > normalCullThreshold;

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
        } else { // No camera, just generate base mesh
             for (auto& tile : tiles_) {
                if (force || tile.currentExponent != 0 || tile.vertexCount == 0) {
                    generateTileGeometry(tile, 0);
                    tile.currentExponent = 0;
                    anyChanged = true;
                }
            }
        }

        if (anyChanged || force || meshDirty_) {
            rebuildMesh();
            meshDirty_ = false;
        }
    }

    void draw() {
        if (mesh_ && mesh_->getIndexCount() > 0) {
            mesh_->draw();
        }
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

                // use neutral colors for areas outside the mapped data
                if (latCenter < -60.0f || latCenter > 60.0f) {
                    tile.color = glm::vec3(0.5f, 0.5f, 0.5f);  // Neutral gray
                }

                tile.centerDirection = glm::normalize(
                    SphericalToCartesian(1.0f, glm::radians(latCenter), glm::radians(lonCenter)));
                tile.centerPosition = tile.centerDirection * radius_;

                const float cosLat = std::abs(std::cos(tile.latCenterRad));
                tile.widthWorld = radius_ * lonSpanRad * std::max(0.001f, cosLat);
                tile.heightWorld = radius_ * latSpanRad;
                tile.maxWorldSpan = std::max(tile.widthWorld, tile.heightWorld);

                tiles_.push_back(std::move(tile));
            }
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
                const glm::vec3 pos = SphericalToCartesian(radius_, lat, lon);
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
        if (!mesh_) return;
        mesh_->vertices.clear();
        mesh_->indices.clear();

        size_t vertexOffset = 0;
        for (const auto& tile : tiles_) {
            if (!tile.visible || tile.vertexCount == 0) continue;
            mesh_->vertices.insert(mesh_->vertices.end(), tile.vertices.begin(), tile.vertices.end());
            for (unsigned int index : tile.indices) {
                mesh_->indices.push_back(static_cast<unsigned int>(index + vertexOffset));
            }
            vertexOffset += tile.vertexCount;
        }

        mesh_->uploadData();

        if (!attributesConfigured_) {
            mesh_->setupVertexAttributes({3, 3, 3});
            attributesConfigured_ = true;
        }
    }

    float radius_;
    std::vector<Tile> tiles_;
    std::unique_ptr<Mesh> mesh_;
    bool meshDirty_ = true;
    bool attributesConfigured_ = false;
};
