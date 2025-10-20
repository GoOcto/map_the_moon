#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

class ColorMapSampler {
  public:
    explicit ColorMapSampler(std::string dataRoot = ".");

    void setDataRoot(std::string dataRoot);
    std::array<float, 3> sample(float u, float v);
    std::vector<std::array<float, 3>> sampleColorsForTerrain(double povLatDegrees, double povLonDegrees, int width,
                                                             int height, float totalLatSpan, float totalLonSpan);
    bool hasData() const;

  private:
    bool load();
    size_t pixelIndex(int x, int y) const;

    mutable std::mutex m_loadMutex;
    bool m_isLoaded = false;
    int m_width = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_colorData;
    std::filesystem::path m_dataRoot;
};
