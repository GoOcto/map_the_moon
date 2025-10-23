#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace terrain {

struct TileMetadata {
    std::string filename;
    double minLatitude;
    double maxLatitude;
    double minLongitude;
    double maxLongitude;
};

constexpr int CHUNK_SIZE = 512;
constexpr int TILE_WIDTH = 23040;
constexpr int TILE_HEIGHT = 15360;
constexpr int NUM_CHUNKS_X = TILE_WIDTH / CHUNK_SIZE;
constexpr int NUM_CHUNKS_Y = TILE_HEIGHT / CHUNK_SIZE;
constexpr double kLongitudeWrap = 360.0;

inline double wrapLongitude(double lonDegrees) {
    double wrapped = std::fmod(lonDegrees, kLongitudeWrap);
    if (wrapped < 0.0) {
        wrapped += kLongitudeWrap;
    }
    return wrapped;
}

inline double longitudeSpan(const TileMetadata& tile) {
    double span = tile.maxLongitude - tile.minLongitude;
    if (span <= 0.0) {
        span += kLongitudeWrap;
    }
    return span;
}

inline bool longitudeInTile(const TileMetadata& tile, double lon) {
    if (tile.minLongitude <= tile.maxLongitude) {
        return lon >= tile.minLongitude && lon <= tile.maxLongitude;
    }
    return lon >= tile.minLongitude || lon <= tile.maxLongitude;
}

inline double longitudeOffsetWithinTile(const TileMetadata& tile, double lonDegrees) {
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

inline const std::vector<TileMetadata>& tiles() {
    static const std::vector<TileMetadata> kTiles = {
        {".data/proc/SLDEM2015_512_60S_30S_000_045_CHUNKED_512.DAT", -60.0, -30.0, 0.0, 45.0},
        {".data/proc/SLDEM2015_512_60S_30S_045_090_CHUNKED_512.DAT", -60.0, -30.0, 45.0, 90.0},
        {".data/proc/SLDEM2015_512_60S_30S_090_135_CHUNKED_512.DAT", -60.0, -30.0, 90.0, 135.0},
        {".data/proc/SLDEM2015_512_60S_30S_135_180_CHUNKED_512.DAT", -60.0, -30.0, 135.0, 180.0},
        {".data/proc/SLDEM2015_512_60S_30S_180_225_CHUNKED_512.DAT", -60.0, -30.0, 180.0, 225.0},
        {".data/proc/SLDEM2015_512_60S_30S_225_270_CHUNKED_512.DAT", -60.0, -30.0, 225.0, 270.0},
        {".data/proc/SLDEM2015_512_60S_30S_270_315_CHUNKED_512.DAT", -60.0, -30.0, 270.0, 315.0},
        {".data/proc/SLDEM2015_512_60S_30S_315_360_CHUNKED_512.DAT", -60.0, -30.0, 315.0, 360.0},

        {".data/proc/SLDEM2015_512_30S_00S_000_045_CHUNKED_512.DAT", -30.0, 0.0, 0.0, 45.0},
        {".data/proc/SLDEM2015_512_30S_00S_045_090_CHUNKED_512.DAT", -30.0, 0.0, 45.0, 90.0},
        {".data/proc/SLDEM2015_512_30S_00S_090_135_CHUNKED_512.DAT", -30.0, 0.0, 90.0, 135.0},
        {".data/proc/SLDEM2015_512_30S_00S_135_180_CHUNKED_512.DAT", -30.0, 0.0, 135.0, 180.0},
        {".data/proc/SLDEM2015_512_30S_00S_180_225_CHUNKED_512.DAT", -30.0, 0.0, 180.0, 225.0},
        {".data/proc/SLDEM2015_512_30S_00S_225_270_CHUNKED_512.DAT", -30.0, 0.0, 225.0, 270.0},
        {".data/proc/SLDEM2015_512_30S_00S_270_315_CHUNKED_512.DAT", -30.0, 0.0, 270.0, 315.0},
        {".data/proc/SLDEM2015_512_30S_00S_315_360_CHUNKED_512.DAT", -30.0, 0.0, 315.0, 360.0},

        {".data/proc/SLDEM2015_512_00N_30N_000_045_CHUNKED_512.DAT", 0.0, 30.0, 0.0, 45.0},
        {".data/proc/SLDEM2015_512_00N_30N_045_090_CHUNKED_512.DAT", 0.0, 30.0, 45.0, 90.0},
        {".data/proc/SLDEM2015_512_00N_30N_090_135_CHUNKED_512.DAT", 0.0, 30.0, 90.0, 135.0},
        {".data/proc/SLDEM2015_512_00N_30N_135_180_CHUNKED_512.DAT", 0.0, 30.0, 135.0, 180.0},
        {".data/proc/SLDEM2015_512_00N_30N_180_225_CHUNKED_512.DAT", 0.0, 30.0, 180.0, 225.0},
        {".data/proc/SLDEM2015_512_00N_30N_225_270_CHUNKED_512.DAT", 0.0, 30.0, 225.0, 270.0},
        {".data/proc/SLDEM2015_512_00N_30N_270_315_CHUNKED_512.DAT", 0.0, 30.0, 270.0, 315.0},
        {".data/proc/SLDEM2015_512_00N_30N_315_360_CHUNKED_512.DAT", 0.0, 30.0, 315.0, 360.0},

        {".data/proc/SLDEM2015_512_30N_60N_000_045_CHUNKED_512.DAT", 30.0, 60.0, 0.0, 45.0},
        {".data/proc/SLDEM2015_512_30N_60N_045_090_CHUNKED_512.DAT", 30.0, 60.0, 45.0, 90.0},
        {".data/proc/SLDEM2015_512_30N_60N_090_135_CHUNKED_512.DAT", 30.0, 60.0, 90.0, 135.0},
        {".data/proc/SLDEM2015_512_30N_60N_135_180_CHUNKED_512.DAT", 30.0, 60.0, 135.0, 180.0},
        {".data/proc/SLDEM2015_512_30N_60N_180_225_CHUNKED_512.DAT", 30.0, 60.0, 180.0, 225.0},
        {".data/proc/SLDEM2015_512_30N_60N_225_270_CHUNKED_512.DAT", 30.0, 60.0, 225.0, 270.0},
        {".data/proc/SLDEM2015_512_30N_60N_270_315_CHUNKED_512.DAT", 30.0, 60.0, 270.0, 315.0},
        {".data/proc/SLDEM2015_512_30N_60N_315_360_CHUNKED_512.DAT", 30.0, 60.0, 315.0, 360.0},
    };
    return kTiles;
}

inline const TileMetadata* findTile(double latDegrees, double lonDegrees) {
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

} // namespace terrain
