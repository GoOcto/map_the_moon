#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "color_map_sampler.hpp"
#include "terrain_dataset.hpp"

class TerrainLoader {
  public:
    using TileInfo = terrain::TileMetadata;

    // --- State for Tile Caching (internal) ---
    struct TileStream {
        std::ifstream file;
        double pixelsPerDegreeX = 0.0;
        double pixelsPerDegreeY = 0.0;
        double degreesPerPixelX = 0.0;
        double degreesPerPixelY = 0.0;
        // The key is a 64-bit integer packing chunkX and chunkY for efficient lookups
        std::unordered_map<int64_t, std::vector<float>> chunkCache;
    };

    // --- Public API ---

    /**
     * @brief Construct the stateful terrain loader.
     * @param dataRoot Path to the DEM data directory.
     */
    TerrainLoader(std::string dataRoot) : m_dataRoot(std::move(dataRoot)) {
    }

    /**
     * @brief The main stateful function. Call this every time you need new terrain data.
     * It will automatically handle full reloads or efficient scrolling.
     * @return A std::vector<float> of elevation data in meters.
     */
    std::vector<float> loadOrUpdateTerrain(double povLatDegrees, double povLonDegrees, int width, int height,
                                           int steps) {

        const TileInfo *newTile = terrain::findTile(povLatDegrees, povLonDegrees);
        if (!newTile) {
            std::cerr << "No terrain tile available for new location." << std::endl;
            // Return old data if we have it, otherwise an empty vector
            return m_isInitialized ? m_elevationData : std::vector<float>();
        }

        bool needsFullLoad = !m_isInitialized || width != m_currentWidth || height != m_currentHeight ||
                             steps != m_currentSteps || newTile->filename != m_currentTileFile;

        if (needsFullLoad) {
            doFullLoad(povLatDegrees, povLonDegrees, width, height, steps, *newTile);
        } else {
            doScrollLoad(povLatDegrees, povLonDegrees, width, height, steps, *newTile);
        }

        // Return a copy of the final elevation data, converted to meters
        std::vector<float> dataInMeters = m_elevationData;
        for (float &val : dataInMeters) {
            val *= 1000.0f; // Convert km to meters
        }
        return dataInMeters;
    }

    /**
     * @brief (STATIC) Generates vertex and index buffers from elevation data.
     * Your app calls this.
     */
    static void generateMesh(const std::vector<float> &elevationData, int width, int height,
                             std::vector<float> &vertices, std::vector<unsigned int> &indices) {
        std::cout << "Generating mesh..." << std::endl;

        float scaleZ = 1.f / 30.325f; //  30.325km/degree at the equator or 30.325/512km per sample, because grid
                                      //  samples are 1 unit apart in X/Y
        vertices.clear();
        indices.clear();
        vertices.reserve(static_cast<size_t>(width) * height * 7);
        indices.reserve(static_cast<size_t>(width - 1) * (height - 1) * 6);

        const bool hasColorMap = ColorMapSampler::ensureLoaded();
        const float invWidth = (width > 1) ? 1.0f / static_cast<float>(width - 1) : 0.0f;
        const float invHeight = (height > 1) ? 1.0f / static_cast<float>(height - 1) : 0.0f;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float elevation = elevationData[static_cast<size_t>(y) * width + x];

                const float mirroredX = static_cast<float>((width - 1) - x);
                vertices.push_back(mirroredX);
                vertices.push_back(static_cast<float>(y));
                vertices.push_back(elevation * scaleZ);
                vertices.push_back(elevation); // Store raw elevation in w-component (or 4th attrib)

                std::array<float, 3> color{0.8f, 0.8f, 0.8f};
                if (hasColorMap) {
                    const float u = static_cast<float>(x) * invWidth;
                    const float v = static_cast<float>(y) * invHeight;
                    color = ColorMapSampler::sample(u, v);
                }

                vertices.push_back(color[0]);
                vertices.push_back(color[1]);
                vertices.push_back(color[2]);
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

        std::cout << "Generated " << vertices.size() / 7 << " vertices and " << indices.size() / 3 << " triangles"
                  << std::endl;
    }

    /**
     * @brief (STATIC) Updates existing vertex Z/W components from new elevation data.
     * Your app calls this.
     */
    static void updateMeshElevations(const std::vector<float> &elevationData, int width, int height,
                                     std::vector<float> &vertices) {
        float scaleZ = 1.f / 30.325f;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                size_t vertexIndex = (static_cast<size_t>(y) * width + x) * 7;
                float elevation = elevationData[static_cast<size_t>(y) * width + x];

                // Ensure vertices vector is large enough (should be)
                if (vertexIndex + 3 < vertices.size()) {
                    vertices[vertexIndex + 2] = elevation * scaleZ;
                    vertices[vertexIndex + 3] = elevation;
                }
            }
        }
    }

  private:
    // --- Member State (for caching and scrolling) ---
    std::string m_dataRoot = ".data";
    std::string m_currentTileFile;

    std::vector<float> m_elevationData; // Internal cache, stored in KM

    double m_currentLat = 0.0;
    double m_currentLon = 0.0;
    int m_currentWidth = 0;
    int m_currentHeight = 0;
    int m_currentSteps = 1;
    int m_currentCenterX = 0; // Center pixel X in source tile
    int m_currentCenterY = 0; // Center pixel Y in source tile

    bool m_isInitialized = false;
    bool m_warnedMissingTile = false;
    std::unordered_map<std::string, TileStream> m_tileCache;

    // --- Private Methods ---

    /**
     * @brief Performs a full, fresh load. Updates internal m_elevationData.
     */
    void doFullLoad(double povLat, double povLon, int width, int height, int steps, const TileInfo &tile) {
        std::cout << "\nPerforming full terrain load..." << std::endl;

        int centerX, centerY, startX, startY;
        double degPerPixelX, degPerPixelY;
        calculateViewParams(tile, povLat, povLon, width, height, steps, centerX, centerY, startX, startY, degPerPixelX,
                            degPerPixelY);

        std::vector<float> newData(static_cast<size_t>(width) * height, 0.0f);
        TileStream *mainStream = ensureTileStream(tile);
        if (!mainStream) {
            std::cerr << "Error: Could not open main tile stream for: " << tile.filename << std::endl;
            if (!m_isInitialized)
                m_elevationData.clear(); // Ensure we return empty
            return;
        }

        // The loading loop is chunk-aware
        for (int row = 0; row < height; ++row) {
            for (int x = 0; x < width; ++x) {
                const int srcY = startY + row * steps;
                const int srcX = startX + x * steps;
                const size_t dataIndex = static_cast<size_t>(row) * width + x;

                if (srcY >= 0 && srcY < terrain::TILE_HEIGHT && srcX >= 0 && srcX < terrain::TILE_WIDTH) {
                    // This pixel is within the main tile, use the efficient chunked reader
                    newData[dataIndex] = getHeightFromChunk(*mainStream, srcX, srcY);
                } else {
                    // This pixel is outside the main tile, use the global lookup as a fallback
                    const double sampleLat = tile.maxLatitude - static_cast<double>(srcY) * degPerPixelY;
                    const double sampleLon = tile.minLongitude + static_cast<double>(srcX) * degPerPixelX;
                    newData[dataIndex] = lookupHeight(sampleLat, sampleLon);
                }
            }
        }

        m_elevationData.swap(newData); // Swap new data into our internal cache

        // Update state
        m_currentLat = povLat;
        m_currentLon = povLon;
        m_currentWidth = width;
        m_currentHeight = height;
        m_currentSteps = steps;
        m_currentCenterX = centerX;
        m_currentCenterY = centerY;
        m_currentTileFile = tile.filename;
        m_isInitialized = true;
        clearCachedChunks();
    }

    /**
     * @brief Performs an efficient "scrolling" update. Updates internal m_elevationData.
     */
    void doScrollLoad(double povLat, double povLon, int width, int height, int steps, const TileInfo &tile) {

        int newCenterX, newCenterY;
        // The rest of these are unused in this calculation but required by the function
        int ignored_startX, ignored_startY;
        double ignored_dppX, ignored_dppY;
        calculateViewParams(tile, povLat, povLon, width, height, steps, newCenterX, newCenterY, ignored_startX,
                            ignored_startY, ignored_dppX, ignored_dppY);

        const int deltaX_src = newCenterX - m_currentCenterX;
        const int deltaY_src = newCenterY - m_currentCenterY;

        const int gridShiftX = static_cast<int>(std::round(static_cast<double>(deltaX_src) / steps));
        const int gridShiftY = static_cast<int>(std::round(static_cast<double>(deltaY_src) / steps));

        if (gridShiftX == 0 && gridShiftY == 0) {
            return; // No change
        }

        if (std::abs(gridShiftX) >= width || std::abs(gridShiftY) >= height) {
            doFullLoad(povLat, povLon, width, height, steps, tile);
            return;
        }

        std::cout << "Scrolling grid by (" << gridShiftX << ", " << gridShiftY << ")" << std::endl;
        std::vector<float> newData(static_cast<size_t>(width) * height, 0.0f);
        std::vector<bool> needsLoading(static_cast<size_t>(width) * height, false);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int oldX = x + gridShiftX;
                const int oldY = y + gridShiftY;
                if (oldX >= 0 && oldX < width && oldY >= 0 && oldY < height) {
                    newData[static_cast<size_t>(y) * width + x] =
                        m_elevationData[static_cast<size_t>(oldY) * width + oldX];
                } else {
                    needsLoading[static_cast<size_t>(y) * width + x] = true;
                }
            }
        }

        const int effectiveNewCenterX = m_currentCenterX + gridShiftX * steps;
        const int effectiveNewCenterY = m_currentCenterY + gridShiftY * steps;
        const int effectiveNewStartX = effectiveNewCenterX - (width * steps) / 2;
        const int effectiveNewStartY = effectiveNewCenterY - (height * steps) / 2;

        const double lonSpan = terrain::longitudeSpan(tile);
        const double latSpan = tile.maxLatitude - tile.minLatitude;
        const double degPerPixelX = lonSpan / static_cast<double>(terrain::TILE_WIDTH);
        const double degPerPixelY = latSpan / static_cast<double>(terrain::TILE_HEIGHT);

        TileStream *mainStream = ensureTileStream(tile);
        if (!mainStream)
            return; // Error, just keep old data

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const size_t dataIndex = static_cast<size_t>(y) * width + x;
                if (!needsLoading[dataIndex])
                    continue;

                const int srcY = effectiveNewStartY + y * steps;
                const int srcX = effectiveNewStartX + x * steps;

                if (srcY >= 0 && srcY < terrain::TILE_HEIGHT && srcX >= 0 && srcX < terrain::TILE_WIDTH) {
                    newData[dataIndex] = getHeightFromChunk(*mainStream, srcX, srcY);
                } else {
                    const double sampleLat = tile.maxLatitude - static_cast<double>(srcY) * degPerPixelY;
                    const double sampleLon = tile.minLongitude + static_cast<double>(srcX) * degPerPixelX;
                    newData[dataIndex] = lookupHeight(sampleLat, sampleLon);
                }
            }
        }

        m_elevationData.swap(newData);
        m_currentLat = povLat;
        m_currentLon = povLon;
        m_currentCenterX = effectiveNewCenterX;
        m_currentCenterY = effectiveNewCenterY;
        clearCachedChunks();
    }

    // --- Tile Loading Helpers ---

    TileStream *ensureTileStream(const TileInfo &info) {
        if (auto it = m_tileCache.find(info.filename); it != m_tileCache.end()) {
            return &it->second;
        }

        TileStream stream;
        const double localLonSpan = terrain::longitudeSpan(info);
        const double localLatSpan = info.maxLatitude - info.minLatitude;
        if (localLonSpan <= 0.0 || localLatSpan <= 0.0) {
            throw std::runtime_error("Invalid tile metadata: zero span");
        }

        const auto path = std::filesystem::path(m_dataRoot) / info.filename;
        stream.file.open(path, std::ios::binary);
        if (!stream.file.is_open()) {
            std::cerr << "Error: Could not open file: " << path.string() << std::endl;
            return nullptr;
        }

        stream.pixelsPerDegreeX = static_cast<double>(terrain::TILE_WIDTH) / localLonSpan;
        stream.pixelsPerDegreeY = static_cast<double>(terrain::TILE_HEIGHT) / localLatSpan;
        stream.degreesPerPixelX = 1.0 / stream.pixelsPerDegreeX;
        stream.degreesPerPixelY = 1.0 / stream.pixelsPerDegreeY;

        auto [emplacedIt, inserted] = m_tileCache.emplace(info.filename, std::move(stream));
        return &emplacedIt->second;
    }

    const std::vector<float> *fetchChunk(TileStream &stream, int chunkX, int chunkY) {
        if (chunkX < 0 || chunkX >= terrain::NUM_CHUNKS_X || chunkY < 0 || chunkY >= terrain::NUM_CHUNKS_Y) {
            return nullptr;
        }

        const int64_t chunkKey = (static_cast<int64_t>(chunkY) << 32) | chunkX;
        if (auto cached = stream.chunkCache.find(chunkKey); cached != stream.chunkCache.end()) {
            return &cached->second;
        }

        const int linearChunkIndex = chunkY * terrain::NUM_CHUNKS_X + chunkX;
        const std::streamoff byteOffset =
            static_cast<std::streamoff>(linearChunkIndex) * terrain::CHUNK_SIZE * terrain::CHUNK_SIZE * sizeof(float);

        std::vector<float> chunkData(terrain::CHUNK_SIZE * terrain::CHUNK_SIZE);

        stream.file.clear();
        stream.file.seekg(byteOffset);
        stream.file.read(reinterpret_cast<char *>(chunkData.data()),
                         static_cast<std::streamsize>(chunkData.size() * sizeof(float)));

        if (!stream.file) {
            stream.file.clear(); // Clear error flags
            std::cerr << "File read error at offset " << byteOffset << std::endl;
            return nullptr;
        }

        auto [it, inserted] = stream.chunkCache.emplace(chunkKey, std::move(chunkData));
        return &it->second;
    }

    // Helper to get a single height value using the chunk system.
    float getHeightFromChunk(TileStream &stream, int pixelX, int pixelY) {
        const int chunkX = pixelX / terrain::CHUNK_SIZE;
        const int chunkY = pixelY / terrain::CHUNK_SIZE;

        const std::vector<float> *chunkData = fetchChunk(stream, chunkX, chunkY);
        if (!chunkData) {
            return 0.0f; // Default value on error
        }

        const int innerX = pixelX % terrain::CHUNK_SIZE;
        const int innerY = pixelY % terrain::CHUNK_SIZE;
        const int innerIndex = innerY * terrain::CHUNK_SIZE + innerX;

        return (*chunkData)[innerIndex];
    }

    float lookupHeight(double latDeg, double lonDeg) {
        if (latDeg < -60.0 || latDeg > 60.0) {
            return 0.0f;
        }

        const double wrappedLon = terrain::wrapLongitude(lonDeg);
        const TileInfo *sampleTile = terrain::findTile(latDeg, lonDeg);
        if (!sampleTile) {
            if (!m_warnedMissingTile) {
                std::cerr << "Warning: No DEM tile for lat=" << latDeg << " lon=" << wrappedLon << std::endl;
                m_warnedMissingTile = true;
            }
            return 0.0f;
        }

        TileStream *stream = ensureTileStream(*sampleTile);
        if (!stream) {
            return 0.0f;
        }

        const double lonOffset = terrain::longitudeOffsetWithinTile(*sampleTile, wrappedLon);
        const double clampedSampleLat = std::clamp(latDeg, sampleTile->minLatitude, sampleTile->maxLatitude);

        int pixelX = static_cast<int>(std::round(lonOffset * stream->pixelsPerDegreeX));
        pixelX = std::clamp(pixelX, 0, terrain::TILE_WIDTH - 1);

        int pixelY =
            static_cast<int>(std::round((sampleTile->maxLatitude - clampedSampleLat) * stream->pixelsPerDegreeY));
        pixelY = std::clamp(pixelY, 0, terrain::TILE_HEIGHT - 1);

        return getHeightFromChunk(*stream, pixelX, pixelY);
    }

    void calculateViewParams(const TileInfo &tile, double povLat, double povLon, int width, int height, int steps,
                             int &outCenterX, int &outCenterY, int &outStartX, int &outStartY, double &outDegPerPixelX,
                             double &outDegPerPixelY) {
        const double lonSpan = terrain::longitudeSpan(tile);
        const double latSpan = tile.maxLatitude - tile.minLatitude;

        const double pixelsPerDegreeX = static_cast<double>(terrain::TILE_WIDTH) / lonSpan;
        const double pixelsPerDegreeY = static_cast<double>(terrain::TILE_HEIGHT) / latSpan;
        outDegPerPixelX = 1.0 / pixelsPerDegreeX;
        outDegPerPixelY = 1.0 / pixelsPerDegreeY;

        const double lonOffsetDegrees = terrain::longitudeOffsetWithinTile(tile, povLon);
        const double clampedLat = std::clamp(povLat, tile.minLatitude, tile.maxLatitude);

        outCenterX = static_cast<int>(std::round(lonOffsetDegrees * pixelsPerDegreeX));
        outCenterY = static_cast<int>(std::round((tile.maxLatitude - clampedLat) * pixelsPerDegreeY));

        outCenterX = std::clamp(outCenterX, 0, terrain::TILE_WIDTH - 1);
        outCenterY = std::clamp(outCenterY, 0, terrain::TILE_HEIGHT - 1);

        const int sampleWidth = width * steps;
        const int sampleHeight = height * steps;
        outStartX = outCenterX - sampleWidth / 2;
        outStartY = outCenterY - sampleHeight / 2;
    }

    void clearCachedChunks() {
        for (auto &entry : m_tileCache) {
            entry.second.chunkCache.clear();
            entry.second.chunkCache.rehash(0);
        }
    }
};
