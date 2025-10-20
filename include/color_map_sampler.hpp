#pragma once

#include <array>
#include <string>

class ColorMapSampler {
public:
    static void setDataRoot(std::string dataRoot);
    static bool ensureLoaded();
    static std::array<float, 3> sample(float u, float v);
    static bool hasData();
};
