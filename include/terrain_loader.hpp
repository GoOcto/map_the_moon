#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

    // --- Constants ---
    static constexpr int MESH_SIZE = 1024;
    static constexpr int TILE_WIDTH = 23040;
    static constexpr int TILE_HEIGHT = 15360;

    // --- State for Tile Caching (internal) ---
    struct TileStream {
        std::ifstream file;
        double pixelsPerDegreeX = 0.0;
        double pixelsPerDegreeY = 0.0;
        double degreesPerPixelX = 0.0;
        double degreesPerPixelY = 0.0;
        std::unordered_map<int, std::vector<float>> rowCache;
    };

    // --- Public API ---

    /**
     * @brief Construct the stateful terrain loader.
     * @param dataRoot Path to the DEM data directory.
     */
    TerrainLoader(std::string dataRoot) : m_dataRoot(std::move(dataRoot)) {}

    /**
     * @brief The main stateful function. Call this every time you need new terrain data.
     * It will automatically handle full reloads or efficient scrolling.
     * @return A std::vector<float> of elevation data in meters.
     */
    std::vector<float> loadOrUpdateTerrain(double povLatDegrees, double povLonDegrees, int width, int height, int steps) {
        
        const TileInfo* newTile = findTile(povLatDegrees, povLonDegrees);
        if (!newTile) {
            std::cerr << "No terrain tile available for new location." << std::endl;
            // Return old data if we have it, otherwise an empty vector
            return m_isInitialized ? m_elevationData : std::vector<float>();
        }

        bool needsFullLoad = !m_isInitialized ||
                             width != m_currentWidth ||
                             height != m_currentHeight ||
                             steps != m_currentSteps ||
                             newTile->filename != m_currentTileFile;
        
        if (needsFullLoad) {
            doFullLoad(povLatDegrees, povLonDegrees, width, height, steps, *newTile);
        } else {
            doScrollLoad(povLatDegrees, povLonDegrees, width, height, steps, *newTile);
        }

        // Return a copy of the final elevation data, converted to meters
        std::vector<float> dataInMeters = m_elevationData;
        for (float& val : dataInMeters) {
            val *= 1000.0f; // Convert km to meters
        }
        return dataInMeters;
    }


    /**
     * @brief (STATIC) Generates vertex and index buffers from elevation data.
     * Your app calls this.
     */
    static void generateMesh(const std::vector<float>& elevationData, int width, int height,
                             std::vector<float>& vertices, std::vector<unsigned int>& indices) {
        std::cout << "Generating mesh..." << std::endl;
        
        float scaleZ = 0.01f;
        vertices.clear();
        indices.clear();
        vertices.reserve(static_cast<size_t>(width) * height * 4);
        indices.reserve(static_cast<size_t>(width - 1) * (height - 1) * 6);
        
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float elevation = elevationData[static_cast<size_t>(y) * width + x];
                
                vertices.push_back(static_cast<float>(x));
                vertices.push_back(static_cast<float>(y));
                vertices.push_back(elevation * scaleZ);
                vertices.push_back(elevation); // Store raw elevation in w-component (or 4th attrib)
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
    
    /**
     * @brief (STATIC) Updates existing vertex Z/W components from new elevation data.
     * Your app calls this.
     */
    static void updateMeshElevations(const std::vector<float>& elevationData, int width, int height,
                                     std::vector<float>& vertices) {
        float scaleZ = 0.01f;
        
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                size_t vertexIndex = (static_cast<size_t>(y) * width + x) * 4;
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
    std::string m_dataRoot = ".data/dem";
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
    void doFullLoad(double povLat, double povLon, int width, int height, int steps, const TileInfo& tile) {
        std::cout << "\nPerforming full terrain load..." << std::endl;
        
        int centerX, centerY, startX, startY;
        double degPerPixelX, degPerPixelY;
        calculateViewParams(tile, povLat, povLon, width, height, steps, 
                            centerX, centerY, startX, startY, degPerPixelX, degPerPixelY);

        std::vector<float> newData(static_cast<size_t>(width) * height, 0.0f);
        TileStream* mainStream = ensureTileStream(tile);
        if (!mainStream) {
            std::cerr << "Error: Could not open main tile stream for: " << tile.filename << std::endl;
            if (!m_isInitialized) m_elevationData.clear(); // Ensure we return empty
            return;
        }

        // Optimized stateless loop
        for (int row = 0; row < height; ++row) {
            for (int x = 0; x < width; ++x) {
                const int srcY = startY + row * steps;
                const int srcX = startX + x * steps;
                const size_t dataIndex = static_cast<size_t>(row) * width + x;

                if (srcY >= 0 && srcY < TILE_HEIGHT && srcX >= 0 && srcX < TILE_WIDTH) {
                    const std::vector<float>* rowData = fetchRow(*mainStream, srcY);
                    if (rowData) {
                        newData[dataIndex] = (*rowData)[srcX];
                    }
                } else {
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
    }

    /**
     * @brief Performs an efficient "scrolling" update. Updates internal m_elevationData.
     */
    void doScrollLoad(double povLat, double povLon, int width, int height, int steps, const TileInfo& tile) {
        
        // --- START OF FIX ---

        // 1. Calculate the *ideal* new center based on camera
        int newCenterX, newCenterY, newStartX_ideal, newStartY_ideal;
        double degPerPixelX_ignored, degPerPixelY_ignored; // We'll recalculate these
        calculateViewParams(tile, povLat, povLon, width, height, steps,
                            newCenterX, newCenterY, newStartX_ideal, newStartY_ideal, 
                            degPerPixelX_ignored, degPerPixelY_ignored);

        // 2. Calculate the *discrete grid shift* based on the *source pixel* delta
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

        // 3. Copy old data to new positions
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int oldX = x + gridShiftX;
                const int oldY = y + gridShiftY;

                if (oldX >= 0 && oldX < width && oldY >= 0 && oldY < height) {
                    newData[y * width + x] = m_elevationData[oldY * width + oldX];
                } else {
                    needsLoading[y * width + x] = true;
                }
            }
        }

        // 4. Calculate the *actual* new grid center and start pixels based on the *discrete shift*
        const int effectiveNewCenterX = m_currentCenterX + gridShiftX * steps;
        const int effectiveNewCenterY = m_currentCenterY + gridShiftY * steps;
        
        const int effectiveNewStartX = effectiveNewCenterX - (width * steps) / 2;
        const int effectiveNewStartY = effectiveNewCenterY - (height * steps) / 2;

        // 5. We need deg/pixel for the fallback lookup
        const double lonSpan = longitudeSpan(tile);
        const double latSpan = tile.maxLatitude - tile.minLatitude;
        const double pixelsPerDegreeX = static_cast<double>(TILE_WIDTH) / lonSpan;
        const double pixelsPerDegreeY = static_cast<double>(TILE_HEIGHT) / latSpan;
        const double degPerPixelX = 1.0 / pixelsPerDegreeX;
        const double degPerPixelY = 1.0 / pixelsPerDegreeY;

        // 6. Load missing regions
        TileStream* mainStream = ensureTileStream(tile);
        if (!mainStream) return; // Error, just keep old data

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const size_t dataIndex = static_cast<size_t>(y) * width + x;
                if (!needsLoading[dataIndex]) {
                    continue; // This pixel was copied
                }

                // Load based on the *effective* new start, not the ideal one
                const int srcY = effectiveNewStartY + y * steps;
                const int srcX = effectiveNewStartX + x * steps;

                if (srcY >= 0 && srcY < TILE_HEIGHT && srcX >= 0 && srcX < TILE_WIDTH) {
                    const std::vector<float>* rowData = fetchRow(*mainStream, srcY);
                    if (rowData) {
                        newData[dataIndex] = (*rowData)[srcX];
                    }
                } else {
                    // Fallback to global lookup
                    const double sampleLat = tile.maxLatitude - static_cast<double>(srcY) * degPerPixelY;
                    const double sampleLon = tile.minLongitude + static_cast<double>(srcX) * degPerPixelX;
                    newData[dataIndex] = lookupHeight(sampleLat, sampleLon);
                }
            }
        }

        // 7. Finalize and update state
        m_elevationData.swap(newData); // Swap new data into our internal cache
        m_currentLat = povLat;
        m_currentLon = povLon;
        
        // Update state to the *actual* new grid center
        m_currentCenterX = effectiveNewCenterX;
        m_currentCenterY = effectiveNewCenterY;

        // --- END OF FIX ---
    }

    // --- Tile Loading Helpers (now member functions) ---

    TileStream* ensureTileStream(const TileInfo& info) {
        auto it = m_tileCache.find(info.filename);
        if (it != m_tileCache.end()) {
            return &it->second;
        }

        TileStream stream;
        const double localLonSpan = longitudeSpan(info);
        const double localLatSpan = info.maxLatitude - info.minLatitude;
        if (localLonSpan <= 0.0 || localLatSpan <= 0.0) {
            throw std::runtime_error("Invalid tile metadata: zero span");
        }

        const std::filesystem::path path = std::filesystem::path(m_dataRoot) / info.filename;
        stream.file.open(path, std::ios::binary);
        if (!stream.file.is_open()) {
            std::cerr << "Error: Could not open file: " << path.string() << std::endl;
            return nullptr;
        }

        stream.pixelsPerDegreeX = static_cast<double>(TILE_WIDTH) / localLonSpan;
        stream.pixelsPerDegreeY = static_cast<double>(TILE_HEIGHT) / localLatSpan;
        stream.degreesPerPixelX = 1.0 / stream.pixelsPerDegreeX;
        stream.degreesPerPixelY = 1.0 / stream.pixelsPerDegreeY;

        auto [emplacedIt, inserted] = m_tileCache.emplace(info.filename, std::move(stream));
        return &emplacedIt->second;
    }

    const std::vector<float>* fetchRow(TileStream& stream, int rowIndex) {
        if (rowIndex < 0 || rowIndex >= TILE_HEIGHT) {
            return nullptr;
        }

        auto cached = stream.rowCache.find(rowIndex);
        if (cached != stream.rowCache.end()) {
            return &cached->second;
        }

        std::vector<float> row(static_cast<size_t>(TILE_WIDTH));
        const std::streamoff byteOffset = static_cast<std::streamoff>(rowIndex) * TILE_WIDTH * sizeof(float);

        stream.file.clear();
        stream.file.seekg(byteOffset);
        stream.file.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float)));
        if (!stream.file) {
            stream.file.clear();
            return nullptr;
        }

        auto [rowIt, inserted] = stream.rowCache.emplace(rowIndex, std::move(row));
        return &rowIt->second;
    }

    float lookupHeight(double latDeg, double lonDeg) {
        if (latDeg < -60.0 || latDeg > 60.0) {
            return 0.0f;
        }

        const double wrappedLon = wrapLongitude(lonDeg);
        const TileInfo* sampleTile = findTile(latDeg, lonDeg);
        if (!sampleTile) {
            if (!m_warnedMissingTile) {
                std::cerr << "Warning: No DEM tile for lat=" << latDeg
                            << " lon=" << wrappedLon << std::endl;
                m_warnedMissingTile = true;
            }
            return 0.0f;
        }

        TileStream* stream = ensureTileStream(*sampleTile);
        if (!stream) {
            return 0.0f;
        }

        const double lonOffset = longitudeOffsetWithinTile(*sampleTile, wrappedLon);
        const double clampedSampleLat = std::clamp(latDeg, sampleTile->minLatitude, sampleTile->maxLatitude);

        int pixelX = static_cast<int>(std::round(lonOffset * stream->pixelsPerDegreeX));
        pixelX = std::clamp(pixelX, 0, TILE_WIDTH - 1);

        int pixelY = static_cast<int>(std::round((sampleTile->maxLatitude - clampedSampleLat) * stream->pixelsPerDegreeY));
        pixelY = std::clamp(pixelY, 0, TILE_HEIGHT - 1);

        const std::vector<float>* rowData = fetchRow(*stream, pixelY);
        if (!rowData) {
            return 0.0f;
        }

        return (*rowData)[pixelX];
    }

    void calculateViewParams(const TileInfo& tile, double povLat, double povLon, 
                             int width, int height, int steps,
                             int& outCenterX, int& outCenterY,
                             int& outStartX, int& outStartY,
                             double& outDegPerPixelX, double& outDegPerPixelY)
    {
        const double lonSpan = longitudeSpan(tile);
        const double latSpan = tile.maxLatitude - tile.minLatitude;
        
        const double pixelsPerDegreeX = static_cast<double>(TILE_WIDTH) / lonSpan;
        const double pixelsPerDegreeY = static_cast<double>(TILE_HEIGHT) / latSpan;
        outDegPerPixelX = 1.0 / pixelsPerDegreeX;
        outDegPerPixelY = 1.0 / pixelsPerDegreeY;

        const double lonOffsetDegrees = longitudeOffsetWithinTile(tile, povLon);
        const double clampedLat = std::clamp(povLat, tile.minLatitude, tile.maxLatitude);

        outCenterX = static_cast<int>(std::round(lonOffsetDegrees * pixelsPerDegreeX));
        outCenterY = static_cast<int>(std::round((tile.maxLatitude - clampedLat) * pixelsPerDegreeY));

        outCenterX = std::clamp(outCenterX, 0, TILE_WIDTH - 1);
        outCenterY = std::clamp(outCenterY, 0, TILE_HEIGHT - 1);

        const int sampleWidth = width * steps;
        const int sampleHeight = height * steps;
        outStartX = outCenterX - sampleWidth / 2;
        outStartY = outCenterY - sampleHeight / 2;
    }


    // --- Static Geo Helpers (can be private or public, static) ---
    static constexpr double kLongitudeWrap = 360.0;

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
        if (latDegrees < -60.0 || latDegrees > 60.0) {
            return nullptr;
        }

        const double wrappedLon = wrapLongitude(lonDegrees);

        for (const auto& tile : tiles()) {
            if (latDegrees >= tile.minLatitude - 1e-6 && latDegrees <= tile.maxLatitude + 1e-6 &&
                longitudeInTile(tile, wrappedLon)) {
                return &tile;
            }
        }
        return nullptr;
    }
};