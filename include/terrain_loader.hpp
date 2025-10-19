#pragma once

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

class TerrainLoader {
public:
    struct TileInfo {
        std::string filename;
        double minLatitude;   // degrees
        double maxLatitude;   // degrees
        double minLongitude;  // degrees in [0, 360)
        double maxLongitude;  // degrees in [0, 360)
    };

    static constexpr int MESH_SIZE = 1024;
    static constexpr int TILE_WIDTH = 23040;
    static constexpr int TILE_HEIGHT = 15360;

    static void setDataRoot(std::string root) {
        dataRoot_ = std::move(root);
    }

    static const std::string& dataRoot() {
        return dataRoot_;
    }

    static std::vector<float> loadLunarData(int width, int height,
                                            double povLatDegrees, double povLonDegrees,
                                            int steps = 1) {
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("TerrainLoader::loadLunarData requires positive width/height");
        }
        if (steps <= 0) {
            throw std::invalid_argument("TerrainLoader::loadLunarData requires steps > 0");
        }

        const TileInfo* tile = findTile(povLatDegrees, povLonDegrees);
        if (!tile) {
            std::cerr << "No terrain tile available for lat=" << povLatDegrees
                      << " lon=" << povLonDegrees << std::endl;
            return {};
        }

        const std::filesystem::path fullPath = std::filesystem::path(dataRoot_) / tile->filename;
        std::cout << "Loading lunar data from: " << fullPath.string() << std::endl;

        std::ifstream file(fullPath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open file: " << fullPath.string() << std::endl;
            return {};
        }

        const double lonSpan = longitudeSpan(*tile);
        const double latSpan = tile->maxLatitude - tile->minLatitude;
        if (lonSpan <= 0.0 || latSpan <= 0.0) {
            throw std::runtime_error("Invalid tile metadata: zero span");
        }

        const double pixelsPerDegreeX = static_cast<double>(TILE_WIDTH) / lonSpan;
        const double pixelsPerDegreeY = static_cast<double>(TILE_HEIGHT) / latSpan;

        const double lonOffsetDegrees = longitudeOffsetWithinTile(*tile, povLonDegrees);
        const double clampedLat = std::clamp(povLatDegrees, tile->minLatitude, tile->maxLatitude);

        int centerX = static_cast<int>(std::round(lonOffsetDegrees * pixelsPerDegreeX));
        int centerY = static_cast<int>(std::round((tile->maxLatitude - clampedLat) * pixelsPerDegreeY));

        centerX = std::clamp(centerX, 0, TILE_WIDTH - 1);
        centerY = std::clamp(centerY, 0, TILE_HEIGHT - 1);

        const int sampleWidth = width * steps;
        const int sampleHeight = height * steps;
        const int startX = centerX - sampleWidth / 2;
        const int startY = centerY - sampleHeight / 2;

        std::cout << "Reading data block at (" << startX << ", " << startY
                  << ") with step " << steps << std::endl;

        std::vector<float> data(static_cast<size_t>(width) * height, 0.0f);
        std::vector<float> rowBuffer(static_cast<size_t>(sampleWidth), 0.0f);

        for (int row = 0; row < height; ++row) {
            std::fill(rowBuffer.begin(), rowBuffer.end(), 0.0f);

            const int srcY = startY + row * steps;
            if (srcY >= 0 && srcY < TILE_HEIGHT) {
                const int srcXStart = startX;
                const int srcXEnd = startX + sampleWidth;
                const int validXStart = std::max(0, srcXStart);
                const int validXEnd = std::min(TILE_WIDTH, srcXEnd);

                if (validXStart < validXEnd) {
                    const size_t samplesToRead = static_cast<size_t>(validXEnd - validXStart);
                    const size_t bufferOffset = static_cast<size_t>(validXStart - srcXStart);

                    const std::int64_t linearIndex = static_cast<std::int64_t>(srcY) * TILE_WIDTH + validXStart;
                    const std::streamoff byteOffset = static_cast<std::streamoff>(linearIndex) * sizeof(float);

                    file.seekg(byteOffset);
                    file.read(reinterpret_cast<char*>(rowBuffer.data() + bufferOffset), samplesToRead * sizeof(float));

                    if (!file) {
                        std::fill(rowBuffer.begin() + bufferOffset,
                                  rowBuffer.begin() + bufferOffset + samplesToRead,
                                  0.0f);
                        file.clear();
                    }
                }
            }

            for (int x = 0; x < width; ++x) {
                const size_t sampleIndex = static_cast<size_t>(x * steps);
                const size_t dataIndex = static_cast<size_t>(row) * width + x;
                data[dataIndex] = sampleIndex < rowBuffer.size() ? rowBuffer[sampleIndex] : 0.0f;
            }
        }

        file.close();

        for (float& val : data) {
            val *= 1000.0f;
        }

        float minElev = *std::min_element(data.begin(), data.end());
        float maxElev = *std::max_element(data.begin(), data.end());
        std::cout << "Elevation range: " << minElev << " to " << maxElev << " meters" << std::endl;

        return data;
    }

    static void generateMesh(const std::vector<float>& elevationData, int width, int height,
                             std::vector<float>& vertices, std::vector<unsigned int>& indices) {
        std::cout << "Generating mesh..." << std::endl;
        
        float scaleZ = 0.01f;
        
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float elevation = elevationData[y * width + x];
                
                vertices.push_back(static_cast<float>(x));
                vertices.push_back(static_cast<float>(y));
                vertices.push_back(elevation * scaleZ);
                vertices.push_back(elevation);
            }
        }
        
        for (int y = 0; y < height - 1; y++) {
            for (int x = 0; x < width - 1; x++) {
                int topLeft = y * width + x;
                int topRight = topLeft + 1;
                int bottomLeft = (y + 1) * width + x;
                int bottomRight = bottomLeft + 1;
                
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);
                
                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }
        
    std::cout << "Generated " << vertices.size() / 4 << " vertices and "
          << indices.size() / 3 << " triangles" << std::endl;
    }
    
    static void updateMeshElevations(const std::vector<float>& elevationData, int width, int height,
                                    std::vector<float>& vertices) {
        float scaleZ = 0.01f;
        
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int vertexIndex = (y * width + x) * 4;
                float elevation = elevationData[y * width + x];
                
                vertices[vertexIndex + 2] = elevation * scaleZ;
                vertices[vertexIndex + 3] = elevation;
            }
        }
    }

private:
    static constexpr double kLongitudeWrap = 360.0;
    inline static std::string dataRoot_ = ".data/dem";

    static const std::vector<TileInfo>& tiles() {
        static const std::vector<TileInfo> kTiles = {
            {"SLDEM2015_512_60S_30S_000_045_FLOAT.IMG", -60.0, -30.0, 0.0, 45.0},
            {"SLDEM2015_512_60S_30S_045_090_FLOAT.IMG", -60.0, -30.0, 45.0, 90.0},
            {"SLDEM2015_512_60S_30S_090_135_FLOAT.IMG", -60.0, -30.0, 90.0, 135.0},
            {"SLDEM2015_512_60S_30S_135_180_FLOAT.IMG", -60.0, -30.0, 135.0, 180.0},
            {"SLDEM2015_512_60S_30S_180_225_FLOAT.IMG", -60.0, -30.0, 180.0, 225.0},
            {"SLDEM2015_512_60S_30S_225_270_FLOAT.IMG", -60.0, -30.0, 225.0, 270.0},
            {"SLDEM2015_512_60S_30S_270_315_FLOAT.IMG", -60.0, -30.0, 270.0, 315.0},
            {"SLDEM2015_512_60S_30S_315_360_FLOAT.IMG", -60.0, -30.0, 315.0, 360.0},

            {"SLDEM2015_512_30S_00S_000_045_FLOAT.IMG", -30.0, 0.0, 0.0, 45.0},
            {"SLDEM2015_512_30S_00S_045_090_FLOAT.IMG", -30.0, 0.0, 45.0, 90.0},
            {"SLDEM2015_512_30S_00S_090_135_FLOAT.IMG", -30.0, 0.0, 90.0, 135.0},
            {"SLDEM2015_512_30S_00S_135_180_FLOAT.IMG", -30.0, 0.0, 135.0, 180.0},
            {"SLDEM2015_512_30S_00S_180_225_FLOAT.IMG", -30.0, 0.0, 180.0, 225.0},
            {"SLDEM2015_512_30S_00S_225_270_FLOAT.IMG", -30.0, 0.0, 225.0, 270.0},
            {"SLDEM2015_512_30S_00S_270_315_FLOAT.IMG", -30.0, 0.0, 270.0, 315.0},
            {"SLDEM2015_512_30S_00S_315_360_FLOAT.IMG", -30.0, 0.0, 315.0, 360.0},

            {"SLDEM2015_512_00N_30N_000_045_FLOAT.IMG", 0.0, 30.0, 0.0, 45.0},
            {"SLDEM2015_512_00N_30N_045_090_FLOAT.IMG", 0.0, 30.0, 45.0, 90.0},
            {"SLDEM2015_512_00N_30N_090_135_FLOAT.IMG", 0.0, 30.0, 90.0, 135.0},
            {"SLDEM2015_512_00N_30N_135_180_FLOAT.IMG", 0.0, 30.0, 135.0, 180.0},
            {"SLDEM2015_512_00N_30N_180_225_FLOAT.IMG", 0.0, 30.0, 180.0, 225.0},
            {"SLDEM2015_512_00N_30N_225_270_FLOAT.IMG", 0.0, 30.0, 225.0, 270.0},
            {"SLDEM2015_512_00N_30N_270_315_FLOAT.IMG", 0.0, 30.0, 270.0, 315.0},
            {"SLDEM2015_512_00N_30N_315_360_FLOAT.IMG", 0.0, 30.0, 315.0, 360.0},

            {"SLDEM2015_512_30N_60N_000_045_FLOAT.IMG", 30.0, 60.0, 0.0, 45.0},
            {"SLDEM2015_512_30N_60N_045_090_FLOAT.IMG", 30.0, 60.0, 45.0, 90.0},
            {"SLDEM2015_512_30N_60N_090_135_FLOAT.IMG", 30.0, 60.0, 90.0, 135.0},
            {"SLDEM2015_512_30N_60N_135_180_FLOAT.IMG", 30.0, 60.0, 135.0, 180.0},
            {"SLDEM2015_512_30N_60N_180_225_FLOAT.IMG", 30.0, 60.0, 180.0, 225.0},
            {"SLDEM2015_512_30N_60N_225_270_FLOAT.IMG", 30.0, 60.0, 225.0, 270.0},
            {"SLDEM2015_512_30N_60N_270_315_FLOAT.IMG", 30.0, 60.0, 270.0, 315.0},
            {"SLDEM2015_512_30N_60N_315_360_FLOAT.IMG", 30.0, 60.0, 315.0, 360.0},
        };
        return kTiles;
    }

    static double wrapLongitude(double lonDegrees) {
        double wrapped = std::fmod(lonDegrees, kLongitudeWrap);
        if (wrapped < 0.0) {
            wrapped += kLongitudeWrap;
        }
        return wrapped;
    }

    static double longitudeSpan(const TileInfo& tile) {
        double span = tile.maxLongitude - tile.minLongitude;
        if (span <= 0.0) {
            span += kLongitudeWrap;
        }
        return span;
    }

    static bool longitudeInTile(const TileInfo& tile, double lon) {
        if (tile.minLongitude <= tile.maxLongitude) {
            return lon >= tile.minLongitude && lon <= tile.maxLongitude;
        }
        return lon >= tile.minLongitude || lon <= tile.maxLongitude;
    }

    static double longitudeOffsetWithinTile(const TileInfo& tile, double lonDegrees) {
        const double lon = wrapLongitude(lonDegrees);
        double delta = lon - tile.minLongitude;
        if (tile.minLongitude > tile.maxLongitude) {
            if (delta < 0.0) {
                delta += kLongitudeWrap;
            }
        }
        double span = longitudeSpan(tile);
        if (delta < 0.0) {
            delta = 0.0;
        }
        if (delta > span) {
            delta = span;
        }
        return delta;
    }

    static const TileInfo* findTile(double latDegrees, double lonDegrees) {
        const double wrappedLon = wrapLongitude(lonDegrees);
        const double clampedLat = std::clamp(latDegrees, -60.0, 60.0);

        for (const auto& tile : tiles()) {
            if (clampedLat >= tile.minLatitude - 1e-6 && clampedLat <= tile.maxLatitude + 1e-6 &&
                longitudeInTile(tile, wrappedLon)) {
                return &tile;
            }
        }

        if (!tiles().empty()) {
            std::cerr << "Warning: No exact tile match for lat=" << latDegrees
                      << " lon=" << lonDegrees << ". Falling back to first tile." << std::endl;
            return &tiles().front();
        }
        return nullptr;
    }
};
