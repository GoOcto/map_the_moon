#pragma once

// NOTE: TINYEXR_IMPLEMENTATION is defined in src/tinyexr_impl.cpp (single TU)
#define TINYEXR_USE_MINIZ (1)
#include "tinyexr.h"
#include "terrain_dataset.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace terrain_exr {

struct EXRTileInfo {
    std::string basePrefix; // e.g. ".data/proc_exr/SLDEM2015_512_00N_30N_000_045"
    double minLatitude  = 0.0;
    double maxLatitude  = 30.0;
    double minLongitude = 0.0;
    double maxLongitude = 45.0;
    int    baseWidth    = 23040;
    int    baseHeight   = 15360;
    int    numLods      = 7;
};

inline const std::vector<EXRTileInfo>& getAvailableTiles() {
    static const std::vector<EXRTileInfo> kTiles = {
        {".data/proc_exr/SLDEM2015_512_00N_30N_000_045",
         0.0, 30.0, 0.0, 45.0, 23040, 15360, 7}
    };
    return kTiles;
}

inline const EXRTileInfo* findTile(double lat, double lon) {
    const double wrappedLon = terrain::wrapLongitude(lon);
    for (const auto& tile : getAvailableTiles()) {
        if (lat  >= tile.minLatitude  && lat  <= tile.maxLatitude &&
            wrappedLon >= tile.minLongitude && wrappedLon <= tile.maxLongitude) {
            return &tile;
        }
    }
    const auto& all = getAvailableTiles();
    return all.empty() ? nullptr : &all[0];
}

} // namespace terrain_exr

// ---------------------------------------------------------------------------

class TerrainLoaderEXR {
public:
    struct LoadedLevel {
        int   lod    = -1;
        int   width  = 0;
        int   height = 0;
        float minElev = 0.f;
        float maxElev = 0.f;
        std::vector<float> data; // row-major float32 elevation in metres
    };

    explicit TerrainLoaderEXR(std::string dataRoot = ".data/proc_exr")
        : m_dataRoot(std::move(dataRoot)) {}

    ~TerrainLoaderEXR() = default;

    int  getActiveLOD()  const { return m_activeLOD; }
    int  getNumLODs()    const { return 7; }

    // Returns a window of elevation data centred on the given lat/lon.
    std::vector<float> loadOrUpdateTerrain(double povLat, double povLon,
                                           int windowW, int windowH,
                                           int requestedLOD) {
        m_activeLOD = std::clamp(requestedLOD, 0, 6);

        const auto* tile = terrain_exr::findTile(povLat, povLon);
        if (!tile) {
            std::cerr << "[EXR] No tile for (" << povLat << ", " << povLon << ")\n";
            return m_windowData;
        }

        const LoadedLevel* lvl = ensureLevelLoaded(*tile, m_activeLOD);
        if (!lvl || lvl->data.empty()) {
            std::cerr << "[EXR] Failed to load LOD " << m_activeLOD << "\n";
            return m_windowData;
        }

        // Map lat/lon -> pixel coordinate in this LOD grid
        const double latSpan = tile->maxLatitude  - tile->minLatitude;
        const double lonSpan = tile->maxLongitude - tile->minLongitude;
        const double nx = std::clamp((povLon - tile->minLongitude) / lonSpan, 0.0, 1.0);
        const double ny = std::clamp((tile->maxLatitude - povLat)  / latSpan, 0.0, 1.0);

        const int cx = static_cast<int>(std::round(nx * (lvl->width  - 1)));
        const int cy = static_cast<int>(std::round(ny * (lvl->height - 1)));

        const int sx = cx - windowW / 2;
        const int sy = cy - windowH / 2;

        std::vector<float> out(static_cast<size_t>(windowW) * windowH, 0.f);
        for (int y = 0; y < windowH; ++y) {
            const int srcY = std::clamp(sy + y, 0, lvl->height - 1);
            for (int x = 0; x < windowW; ++x) {
                const int srcX = std::clamp(sx + x, 0, lvl->width - 1);
                out[static_cast<size_t>(y) * windowW + x] =
                    lvl->data[static_cast<size_t>(srcY) * lvl->width + srcX];
            }
        }

        m_windowData       = std::move(out);
        m_currentTilePrefix = tile->basePrefix;
        return m_windowData;
    }

private:
    std::string        m_dataRoot;
    std::string        m_currentTilePrefix;
    int                m_activeLOD = 4; // 1440x960 loads instantly at startup
    std::vector<float> m_windowData;

    std::unordered_map<int, LoadedLevel> m_levelCache;

    const LoadedLevel* ensureLevelLoaded(const terrain_exr::EXRTileInfo& tile, int lod) {
        if (auto it = m_levelCache.find(lod); it != m_levelCache.end())
            return &it->second;

        const std::string path = tile.basePrefix + "_lod" + std::to_string(lod) + ".exr";
        std::cout << "[EXR] Loading LOD " << lod << " <- " << path << " ...\n";
        const auto t0 = std::chrono::high_resolution_clock::now();

        // --- Parse version ---
        EXRVersion ver;
        if (ParseEXRVersionFromFile(&ver, path.c_str()) != 0) {
            std::cerr << "[EXR] Cannot parse version: " << path << "\n";
            return nullptr;
        }

        // --- Parse header ---
        EXRHeader hdr;
        InitEXRHeader(&hdr);
        const char* err = nullptr;
        if (ParseEXRHeaderFromFile(&hdr, &ver, path.c_str(), &err) != 0) {
            std::cerr << "[EXR] Cannot parse header: " << (err ? err : "?") << "\n";
            if (err) FreeEXRErrorMessage(err);
            return nullptr;
        }

        // --- Load image ---
        EXRImage img;
        InitEXRImage(&img);
        if (LoadEXRImageFromFile(&img, &hdr, path.c_str(), &err) != 0) {
            std::cerr << "[EXR] Cannot load image: " << (err ? err : "?") << "\n";
            if (err) FreeEXRErrorMessage(err);
            FreeEXRHeader(&hdr);
            return nullptr;
        }

        // --- Copy into cache ---
        LoadedLevel lvl;
        lvl.lod    = lod;
        lvl.width  = img.width;
        lvl.height = img.height;
        const size_t total = static_cast<size_t>(img.width) * img.height;
        lvl.data.resize(total);

        if (img.images && img.images[0]) {
            const float* src = reinterpret_cast<const float*>(img.images[0]);
            std::copy(src, src + total, lvl.data.begin());
            lvl.minElev = *std::min_element(lvl.data.begin(), lvl.data.end());
            lvl.maxElev = *std::max_element(lvl.data.begin(), lvl.data.end());
        }

        FreeEXRImage(&img);
        FreeEXRHeader(&hdr);

        const auto ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - t0).count();
        std::cout << "[EXR] LOD " << lod << " (" << lvl.width << "x" << lvl.height
                  << ") loaded in " << ms << " ms, elev ["
                  << lvl.minElev << " m, " << lvl.maxElev << " m]\n";

        auto [it, ok] = m_levelCache.emplace(lod, std::move(lvl));
        return &it->second;
    }
};
