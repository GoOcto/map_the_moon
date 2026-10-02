#pragma once

#define TINYEXR_USE_MINIZ (1)
#include "tinyexr.h"
#include "terrain_dataset.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace terrain_exr {

struct EXRTileInfo {
    std::string basePrefix; // e.g. ".data/proc_exr/SLDEM2015_512_00N_30N_000_045"
    double minLatitude = 0.0;
    double maxLatitude = 30.0;
    double minLongitude = 0.0;
    double maxLongitude = 45.0;
    int baseWidth = 23040;
    int baseHeight = 15360;
    int numLods = 7;
};

inline const std::vector<EXRTileInfo>& getAvailableTiles() {
    static const std::vector<EXRTileInfo> kTiles = {
        {".data/proc_exr/SLDEM2015_512_00N_30N_000_045", 0.0, 30.0, 0.0, 45.0, 23040, 15360, 7}
    };
    return kTiles;
}

inline const EXRTileInfo* findTile(double lat, double lon) {
    const double wrappedLon = terrain::wrapLongitude(lon);
    for (const auto& tile : getAvailableTiles()) {
        if (lat >= tile.minLatitude && lat <= tile.maxLatitude &&
            wrappedLon >= tile.minLongitude && wrappedLon <= tile.maxLongitude) {
            return &tile;
        }
    }
    const auto& all = getAvailableTiles();
    return all.empty() ? nullptr : &all[0];
}

} // namespace terrain_exr

class TerrainLoaderEXR {
public:
    struct LoadedLevel {
        int lod = -1;
        int width = 0;
        int height = 0;
        std::vector<float> data; // Row-major float32 elevations in meters
        float minElev = 0.0f;
        float maxElev = 0.0f;
    };

    TerrainLoaderEXR(std::string dataRoot = ".data/proc_exr") 
        : m_dataRoot(std::move(dataRoot)) {}

    ~TerrainLoaderEXR() {
        clearCache();
    }

    void clearCache() {
        m_levelCache.clear();
        m_currentTilePrefix.clear();
    }

    int getActiveLOD() const { return m_activeLOD; }
    void setActiveLOD(int lod) { m_activeLOD = std::clamp(lod, 0, 6); }
    int getNumLODs() const { return 7; }

    std::vector<float> loadOrUpdateTerrain(double povLatDegrees, double povLonDegrees, 
                                           int windowWidth, int windowHeight, int requestedLOD) {
        m_activeLOD = std::clamp(requestedLOD, 0, 6);

        const auto* tile = terrain_exr::findTile(povLatDegrees, povLonDegrees);
        if (!tile) {
            std::cerr << "[TerrainLoaderEXR] No tile available for (" << povLatDegrees << ", " << povLonDegrees << ")" << std::endl;
            return m_currentWindowData;
        }

        const LoadedLevel* lvl = ensureLevelLoaded(*tile, m_activeLOD);
        if (!lvl || lvl->data.empty()) {
            std::cerr << "[TerrainLoaderEXR] Failed to load LOD " << m_activeLOD << std::endl;
            return m_currentWindowData;
        }

        const double latSpan = tile->maxLatitude - tile->minLatitude;
        const double lonSpan = tile->maxLongitude - tile->minLongitude;
        
        const double normX = std::clamp((povLonDegrees - tile->minLongitude) / lonSpan, 0.0, 1.0);
        const double normY = std::clamp((tile->maxLatitude - povLatDegrees) / latSpan, 0.0, 1.0);

        const int centerX = static_cast<int>(std::round(normX * (lvl->width - 1)));
        const int centerY = static_cast<int>(std::round(normY * (lvl->height - 1)));

        const int startX = centerX - windowWidth / 2;
        const int startY = centerY - windowHeight / 2;

        std::vector<float> windowData(static_cast<size_t>(windowWidth) * windowHeight, 0.0f);

        for (int y = 0; y < windowHeight; ++y) {
            const int srcY = std::clamp(startY + y, 0, lvl->height - 1);
            const size_t rowOffset = static_cast<size_t>(srcY) * lvl->width;

            for (int x = 0; x < windowWidth; ++x) {
                const int srcX = std::clamp(startX + x, 0, lvl->width - 1);
                windowData[static_cast<size_t>(y) * windowWidth + x] = lvl->data[rowOffset + srcX];
            }
        }

        m_currentWindowData = std::move(windowData);
        m_currentLat = povLatDegrees;
        m_currentLon = povLonDegrees;
        m_currentTilePrefix = tile->basePrefix;

        return m_currentWindowData;
    }

private:
    std::string m_dataRoot;
    std::string m_currentTilePrefix;
    int m_activeLOD = 4; // Start at LOD 4 (1440x960) for instant startup (<20ms)
    double m_currentLat = 0.0;
    double m_currentLon = 0.0;

    std::vector<float> m_currentWindowData;
    std::unordered_map<int, LoadedLevel> m_levelCache;

    const LoadedLevel* ensureLevelLoaded(const terrain_exr::EXRTileInfo& tile, int lod) {
        if (auto it = m_levelCache.find(lod); it != m_levelCache.end()) {
            return &it->second;
        }

        std::string lodFile = tile.basePrefix + "_lod" + std::to_string(lod) + ".exr";
        std::cout << "[TerrainLoaderEXR] Loading LOD " << lod << " from " << lodFile << "..." << std::endl;
        auto t0 = std::chrono::high_resolution_clock::now();

        EXRVersion exr_version;
        int ret = ParseEXRVersionFromFile(&exr_version, lodFile.c_str());
        if (ret != 0) {
            std::cerr << "[TerrainLoaderEXR] Error: Cannot parse version for " << lodFile << std::endl;
            return nullptr;
        }

        EXRHeader exr_header;
        InitEXRHeader(&exr_header);
        const char* err = nullptr;
        ret = ParseEXRHeaderFromFile(&exr_header, &exr_version, lodFile.c_str(), &err);
        if (ret != 0) {
            std::cerr << "[TerrainLoaderEXR] Error parsing header: " << (err ? err : "unknown") << std::endl;
            if (err) FreeEXRErrorMessage(err);
            return nullptr;
        }

        EXRImage exr_image;
        InitEXRImage(&exr_image);
        ret = LoadEXRImageFromFile(&exr_image, &exr_header, lodFile.c_str(), &err);
        if (ret != 0) {
            std::cerr << "[TerrainLoaderEXR] Error loading image: " << (err ? err : "unknown") << std::endl;
            if (err) FreeEXRErrorMessage(err);
            FreeEXRHeader(&exr_header);
            return nullptr;
        }

        LoadedLevel lvl;
        lvl.lod = lod;
        lvl.width = exr_image.width;
        lvl.height = exr_image.height;
        const size_t totalPixels = static_cast<size_t>(lvl.width) * lvl.height;
        lvl.data.resize(totalPixels);

        if (exr_image.images != nullptr && exr_image.images[0] != nullptr) {
            const float* src = reinterpret_cast<const float*>(exr_image.images[0]);
            std::copy(src, src + totalPixels, lvl.data.begin());
            lvl.minElev = *std::min_element(lvl.data.begin(), lvl.data.end());
            lvl.maxElev = *std::max_element(lvl.data.begin(), lvl.data.end());
        }

        FreeEXRImage(&exr_image);
        FreeEXRHeader(&exr_header);

        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "[TerrainLoaderEXR] LOD " << lod << " (" << lvl.width << "x" << lvl.height 
                  << ") loaded in " << ms << " ms! Elev range: [" << lvl.minElev << " m, " << lvl.maxElev << " m]" << std::endl;

        auto [emplaced, ok] = m_levelCache.emplace(lod, std::move(lvl));
        return &emplaced->second;
    }
};
