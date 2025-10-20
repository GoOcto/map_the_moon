#pragma once

#include "terrain_dataset.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class TerrainTileCache {
  public:
    struct TileRequest {
        double latStartDeg = 0.0;
        double lonStartDeg = 0.0;
        int resolution = 0; // Samples per edge (rows == cols)
    };

    struct TileSample {
        std::vector<float> heights; // Elevation in meters
        float minElevation = 0.0f;
        float maxElevation = 0.0f;
        int resolution = 0;
    };

    TerrainTileCache(std::string dataRoot, std::size_t maxCachedTiles)
        : dataRoot_(std::move(dataRoot)), maxCachedTiles_(std::max<std::size_t>(1, maxCachedTiles)) {
    }

    TerrainTileCache(std::string dataRoot) : TerrainTileCache(std::move(dataRoot), kDefaultMaxTiles) {
    }

    const TileSample* fetchTile(const TileRequest& request) {
        if (request.resolution <= 1) {
            return nullptr;
        }

        TileKey key = makeKey(request);
        if (auto it = cache_.find(key); it != cache_.end()) {
            touch(it);
            return &it->second.data;
        }

        TileSample sample;
        if (!loadTile(request, sample)) {
            return nullptr;
        }

        evictIfNeeded();
        auto listIt = lruList_.emplace(lruList_.begin(), key);
        auto [insertIt, inserted] = cache_.emplace(key, CacheEntry{std::move(sample), listIt});
        if (!inserted) {
            lruList_.erase(listIt);
            return &insertIt->second.data;
        }
        return &insertIt->second.data;
    }

    void clear() {
        cache_.clear();
        lruList_.clear();
    }

  private:
    struct TileKey {
        int latDegStart = 0;
        int lonDegWrapped = 0;
        int resolution = 0;

        friend bool operator==(const TileKey& lhs, const TileKey& rhs) noexcept {
            return lhs.latDegStart == rhs.latDegStart && lhs.lonDegWrapped == rhs.lonDegWrapped &&
                   lhs.resolution == rhs.resolution;
        }
    };

    struct TileKeyHash {
        std::size_t operator()(const TileKey& key) const noexcept {
            std::size_t seed = static_cast<std::size_t>(key.latDegStart + 180);
            seed = seed * 397u + static_cast<std::size_t>(key.lonDegWrapped);
            seed = seed * 397u + static_cast<std::size_t>(key.resolution);
            return seed;
        }
    };

    struct TileStream {
        std::ifstream file;
        double pixelsPerDegreeX = 0.0;
        double pixelsPerDegreeY = 0.0;
        std::unordered_map<std::int64_t, std::vector<float>> chunkCache;
    };

    struct CacheEntry {
        TileSample data;
        std::list<TileKey>::iterator lruIt;
    };

    static constexpr std::size_t kDefaultMaxTiles = 128;

    std::string dataRoot_;
    std::size_t maxCachedTiles_ = kDefaultMaxTiles;

    std::list<TileKey> lruList_;
    std::unordered_map<TileKey, CacheEntry, TileKeyHash> cache_;
    std::unordered_map<std::string, TileStream> streams_;

    static TileKey makeKey(const TileRequest& request) {
        TileKey key;
        key.latDegStart = static_cast<int>(std::lround(request.latStartDeg));
        int wrapped = static_cast<int>(std::lround(request.lonStartDeg));
        wrapped = ((wrapped % 360) + 360) % 360;
        key.lonDegWrapped = wrapped;
        key.resolution = request.resolution;
        return key;
    }

    void touch(typename std::unordered_map<TileKey, CacheEntry, TileKeyHash>::iterator it) {
        lruList_.splice(lruList_.begin(), lruList_, it->second.lruIt);
        it->second.lruIt = lruList_.begin();
    }

    void evictIfNeeded() {
        while (cache_.size() >= maxCachedTiles_) {
            const TileKey& oldKey = lruList_.back();
            cache_.erase(oldKey);
            lruList_.pop_back();
        }
    }

    const terrain::TileMetadata* findTileForRequest(const TileRequest& request) const {
        const double latCenter = request.latStartDeg + 0.5;
        const double lonCenter = request.lonStartDeg + 0.5;
        return terrain::findTile(latCenter, lonCenter);
    }

    bool loadTile(const TileRequest& request, TileSample& outSample) {
        const terrain::TileMetadata* meta = findTileForRequest(request);
        if (!meta) {
            return false;
        }

        outSample.heights.clear();
        outSample.heights.resize(static_cast<std::size_t>(request.resolution) * request.resolution, 0.0f);
        outSample.minElevation = std::numeric_limits<float>::max();
        outSample.maxElevation = std::numeric_limits<float>::lowest();
        outSample.resolution = request.resolution;

        const double latSpan = 1.0;
        const double lonSpan = 1.0;
        const double latStep = latSpan / static_cast<double>(request.resolution - 1);
        const double lonStep = lonSpan / static_cast<double>(request.resolution - 1);

        for (int r = 0; r < request.resolution; ++r) {
            const double lat = request.latStartDeg + static_cast<double>(r) * latStep;
            for (int c = 0; c < request.resolution; ++c) {
                const double lon = request.lonStartDeg + static_cast<double>(c) * lonStep;
                const float heightMeters = sampleHeight(lat, lon);
                const std::size_t index = static_cast<std::size_t>(r) * request.resolution + c;
                outSample.heights[index] = heightMeters;
                outSample.minElevation = std::min(outSample.minElevation, heightMeters);
                outSample.maxElevation = std::max(outSample.maxElevation, heightMeters);
            }
        }

        if (outSample.minElevation == std::numeric_limits<float>::max()) {
            outSample.minElevation = 0.0f;
            outSample.maxElevation = 0.0f;
        }

        clearAllChunkCaches();
        return true;
    }

    TileStream* ensureStream(const terrain::TileMetadata& meta) {
        if (auto it = streams_.find(meta.filename); it != streams_.end()) {
            return &it->second;
        }

        TileStream stream;
        const double lonSpan = terrain::longitudeSpan(meta);
        const double latSpan = meta.maxLatitude - meta.minLatitude;
        if (lonSpan <= 0.0 || latSpan <= 0.0) {
            return nullptr;
        }

        const std::filesystem::path path = std::filesystem::path(dataRoot_) / meta.filename;
        stream.file.open(path, std::ios::binary);
        if (!stream.file.is_open()) {
            return nullptr;
        }

        stream.pixelsPerDegreeX = static_cast<double>(terrain::TILE_WIDTH) / lonSpan;
        stream.pixelsPerDegreeY = static_cast<double>(terrain::TILE_HEIGHT) / latSpan;

        auto [emplacedIt, inserted] = streams_.emplace(meta.filename, std::move(stream));
        return &emplacedIt->second;
    }

    const std::vector<float>* fetchChunk(TileStream& stream, int chunkX, int chunkY) {
        if (chunkX < 0 || chunkX >= terrain::NUM_CHUNKS_X || chunkY < 0 || chunkY >= terrain::NUM_CHUNKS_Y) {
            return nullptr;
        }

        const std::int64_t chunkKey = (static_cast<std::int64_t>(chunkY) << 32) | chunkX;
        if (auto cached = stream.chunkCache.find(chunkKey); cached != stream.chunkCache.end()) {
            return &cached->second;
        }

        const int linearIndex = chunkY * terrain::NUM_CHUNKS_X + chunkX;
        const std::streamoff byteOffset =
            static_cast<std::streamoff>(linearIndex) * terrain::CHUNK_SIZE * terrain::CHUNK_SIZE * sizeof(float);

        std::vector<float> chunk(terrain::CHUNK_SIZE * terrain::CHUNK_SIZE);

        stream.file.clear();
        stream.file.seekg(byteOffset);
        stream.file.read(reinterpret_cast<char*>(chunk.data()),
                         static_cast<std::streamsize>(chunk.size() * sizeof(float)));
        if (!stream.file) {
            stream.file.clear();
            return nullptr;
        }

        auto [cacheIt, inserted] = stream.chunkCache.emplace(chunkKey, std::move(chunk));
        return &cacheIt->second;
    }

    float getHeightFromChunk(TileStream& stream, int pixelX, int pixelY) {
        const int chunkX = pixelX / terrain::CHUNK_SIZE;
        const int chunkY = pixelY / terrain::CHUNK_SIZE;
        const std::vector<float>* chunk = fetchChunk(stream, chunkX, chunkY);
        if (!chunk) {
            return 0.0f;
        }

        const int innerX = pixelX % terrain::CHUNK_SIZE;
        const int innerY = pixelY % terrain::CHUNK_SIZE;
        const int innerIndex = innerY * terrain::CHUNK_SIZE + innerX;
        if (innerIndex < 0 || innerIndex >= static_cast<int>(chunk->size())) {
            return 0.0f;
        }
        return (*chunk)[innerIndex];
    }

    float sampleHeight(double latDeg, double lonDeg) {
        const terrain::TileMetadata* meta = terrain::findTile(latDeg, lonDeg);
        if (!meta) {
            return 0.0f;
        }

        TileStream* stream = ensureStream(*meta);
        if (!stream) {
            return 0.0f;
        }

        const double wrappedLon = terrain::wrapLongitude(lonDeg);
        const double lonOffset = terrain::longitudeOffsetWithinTile(*meta, wrappedLon);
        const double clampedLat = std::clamp(latDeg, meta->minLatitude, meta->maxLatitude);

        int pixelX = static_cast<int>(std::round(lonOffset * stream->pixelsPerDegreeX));
        pixelX = std::clamp(pixelX, 0, terrain::TILE_WIDTH - 1);

        const double latOffset = meta->maxLatitude - clampedLat;
        int pixelY = static_cast<int>(std::round(latOffset * stream->pixelsPerDegreeY));
        pixelY = std::clamp(pixelY, 0, terrain::TILE_HEIGHT - 1);

        const float heightKm = getHeightFromChunk(*stream, pixelX, pixelY);
        return heightKm * 1000.0f; // Convert to meters
    }

    void clearAllChunkCaches() {
        for (auto& entry : streams_) {
            entry.second.chunkCache.clear();
            entry.second.chunkCache.rehash(0);
        }
    }
};
