#pragma once

#include <array>
#include <string>
#include <vector>

class ColorMapSampler {
  public:
    static void setDataRoot(std::string dataRoot);
    static bool ensureLoaded();
    static std::array<float, 3> sample(float u, float v);
    static std::vector<std::array<float, 3>> sampleColorsForTerrain(double povLatDegrees, double povLonDegrees,
                                                                    int width, int height, float totalLatSpan,
                                                                    float totalLonSpan);
    static bool hasData();
};
