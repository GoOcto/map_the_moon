#include "color_map_sampler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "tinytiffreader.h"

namespace {
//constexpr char kColorMapPath[] = ".data/color/colormap.jpg";
constexpr char kColorMapPath[] = ".data/color/colormap-1kmpp.tif";
constexpr unsigned int kMaxDimension = 4096;
constexpr size_t kChannels = 3;
constexpr std::array<float, 3> kFallbackColor{1.0f, 1.0f, 1.0f};
constexpr std::array<float, 3> kOutOfRangeColor{0.7f, 0.7f, 0.7f};

struct LoadedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

bool isTiffFile(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension == ".tif" || extension == ".tiff";
}

bool loadImageWithStb(const std::filesystem::path& path, LoadedImage& imageOut) {
    int width = 0;
    int height = 0;
    int channelsInFile = 0;

    stbi_set_flip_vertically_on_load(false);
    stbi_uc* rawData = stbi_load(path.string().c_str(), &width, &height, &channelsInFile, static_cast<int>(kChannels));
    if (!rawData) {
        std::cerr << "Failed to load color map: " << path.string() << " Reason: "
                  << (stbi_failure_reason() ? stbi_failure_reason() : "unknown") << std::endl;
        return false;
    }

    const auto imageDeleter = [](stbi_uc* data) { stbi_image_free(data); };
    std::unique_ptr<stbi_uc, decltype(imageDeleter)> imageData(rawData, imageDeleter);

    if (width <= 0 || height <= 0) {
        std::cerr << "Invalid dimensions for color map." << std::endl;
        return false;
    }

    const size_t totalSize = static_cast<size_t>(width) * static_cast<size_t>(height) * kChannels;
    imageOut.width = width;
    imageOut.height = height;
    imageOut.pixels.assign(imageData.get(), imageData.get() + totalSize);
    return true;
}

bool loadImageWithTinyTiff(const std::filesystem::path& path, LoadedImage& imageOut) {
    struct TinyTiffCloser {
        void operator()(TinyTIFFReaderFile* file) const {
            if (file) {
                TinyTIFFReader_close(file);
            }
        }
    };

    std::unique_ptr<TinyTIFFReaderFile, TinyTiffCloser> tiff(
        TinyTIFFReader_open(path.string().c_str()));
    if (!tiff) {
        std::cerr << "Failed to open TIFF color map: " << path.string() << std::endl;
        return false;
    }

    const uint32_t width = TinyTIFFReader_getWidth(tiff.get());
    const uint32_t height = TinyTIFFReader_getHeight(tiff.get());
    if (width == 0 || height == 0) {
        std::cerr << "TIFF color map has no image data: " << path.string() << std::endl;
        return false;
    }

    const uint16_t bitsPerSample = TinyTIFFReader_getBitsPerSample(tiff.get(), 0);
    const uint16_t samplesPerPixel = TinyTIFFReader_getSamplesPerPixel(tiff.get());
    const uint16_t sampleFormat = TinyTIFFReader_getSampleFormat(tiff.get());

    if (bitsPerSample != 8 || sampleFormat != TINYTIFF_SAMPLEFORMAT_UINT) {
        std::cerr << "Unsupported TIFF pixel format in color map (bitsPerSample=" << bitsPerSample
                  << ", sampleFormat=" << sampleFormat << ")." << std::endl;
        return false;
    }

    if (samplesPerPixel == 0) {
        std::cerr << "TIFF color map reports zero samples per pixel." << std::endl;
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    std::vector<std::vector<std::uint8_t>> samplePlanes(samplesPerPixel);
    for (uint16_t sampleIndex = 0; sampleIndex < samplesPerPixel; ++sampleIndex) {
        auto& plane = samplePlanes[sampleIndex];
        plane.resize(pixelCount);
        if (!TinyTIFFReader_getSampleData_s(tiff.get(), plane.data(), static_cast<unsigned long>(plane.size()),
                                            sampleIndex)) {
            const char* error = TinyTIFFReader_getLastError(tiff.get());
            std::cerr << "Failed to read TIFF sample " << sampleIndex << ": "
                      << (error ? error : "unknown error") << std::endl;
            return false;
        }
    }

    imageOut.width = static_cast<int>(width);
    imageOut.height = static_cast<int>(height);
    imageOut.pixels.resize(pixelCount * kChannels);

    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        for (size_t channel = 0; channel < kChannels; ++channel) {
            const size_t sourceChannel = channel < samplePlanes.size() ? channel : 0;
            imageOut.pixels[pixelIndex * kChannels + channel] =
                samplePlanes[sourceChannel][pixelIndex];
        }
    }

    return true;
}

std::vector<std::uint8_t> resampleBilinear(const std::uint8_t* source, int sourceWidth, int sourceHeight,
                                            int destWidth, int destHeight) {
    std::vector<std::uint8_t> destination(static_cast<size_t>(destWidth) * static_cast<size_t>(destHeight) *
                                          kChannels);

    if (destWidth == 0 || destHeight == 0) {
        return destination;
    }

    const float scaleX = static_cast<float>(sourceWidth) / static_cast<float>(destWidth);
    const float scaleY = static_cast<float>(sourceHeight) / static_cast<float>(destHeight);

    for (int y = 0; y < destHeight; ++y) {
        const float rawSrcY = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
        const float clampedSrcY = std::clamp(rawSrcY, 0.0f, static_cast<float>(sourceHeight - 1));
        const int y0 = static_cast<int>(std::floor(clampedSrcY));
        const int y1 = std::min(y0 + 1, sourceHeight - 1);
        const float ty = clampedSrcY - static_cast<float>(y0);

        for (int x = 0; x < destWidth; ++x) {
            const float rawSrcX = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
            const float clampedSrcX = std::clamp(rawSrcX, 0.0f, static_cast<float>(sourceWidth - 1));
            const int x0 = static_cast<int>(std::floor(clampedSrcX));
            const int x1 = std::min(x0 + 1, sourceWidth - 1);
            const float tx = clampedSrcX - static_cast<float>(x0);

            const size_t destIndex =
                (static_cast<size_t>(y) * static_cast<size_t>(destWidth) + static_cast<size_t>(x)) * kChannels;
            const size_t idx00 =
                (static_cast<size_t>(y0) * static_cast<size_t>(sourceWidth) + static_cast<size_t>(x0)) * kChannels;
            const size_t idx10 =
                (static_cast<size_t>(y0) * static_cast<size_t>(sourceWidth) + static_cast<size_t>(x1)) * kChannels;
            const size_t idx01 =
                (static_cast<size_t>(y1) * static_cast<size_t>(sourceWidth) + static_cast<size_t>(x0)) * kChannels;
            const size_t idx11 =
                (static_cast<size_t>(y1) * static_cast<size_t>(sourceWidth) + static_cast<size_t>(x1)) * kChannels;

            for (size_t channel = 0; channel < kChannels; ++channel) {
                const float c00 = static_cast<float>(source[idx00 + channel]);
                const float c10 = static_cast<float>(source[idx10 + channel]);
                const float c01 = static_cast<float>(source[idx01 + channel]);
                const float c11 = static_cast<float>(source[idx11 + channel]);

                const float a = c00 + (c10 - c00) * tx;
                const float b = c01 + (c11 - c01) * tx;
                float value = a + (b - a) * ty;
                value = std::clamp(value, 0.0f, 255.0f);
                destination[destIndex + channel] = static_cast<std::uint8_t>(std::lround(value));
            }
        }
    }

    return destination;
}
} // namespace

ColorMapSampler::ColorMapSampler(std::string dataRoot) {
    setDataRoot(std::move(dataRoot));
}

void ColorMapSampler::setDataRoot(std::string dataRoot) {
    std::scoped_lock lock(m_loadMutex);
    if (dataRoot.empty()) {
        m_dataRoot = std::filesystem::path{"."};
    } else {
        m_dataRoot = std::filesystem::path(std::move(dataRoot));
    }

    m_isLoaded = false;
    m_width = 0;
    m_height = 0;
    m_colorData.clear();
}

bool ColorMapSampler::load() {
    std::scoped_lock lock(m_loadMutex);

    if (m_isLoaded) {
        return true;
    }

    const auto resetState = [this]() {
        m_colorData.clear();
        m_width = 0;
        m_height = 0;
        m_isLoaded = false;
    };

    const std::filesystem::path path = m_dataRoot / kColorMapPath;
    if (!std::filesystem::exists(path)) {
        std::cerr << "Color map not found at " << path.string() << std::endl;
        resetState();
        return false;
    }

    LoadedImage loadedImage;
    const bool isTiff = isTiffFile(path);
    const bool loaded = isTiff ? loadImageWithTinyTiff(path, loadedImage) : loadImageWithStb(path, loadedImage);
    if (!loaded) {
        resetState();
        return false;
    }

    const int srcWidth = loadedImage.width;
    const int srcHeight = loadedImage.height;

    int targetWidth = srcWidth;
    int targetHeight = srcHeight;
    if (static_cast<unsigned int>(targetWidth) > kMaxDimension ||
        static_cast<unsigned int>(targetHeight) > kMaxDimension) {
        if (targetWidth >= targetHeight) {
            targetWidth = static_cast<int>(kMaxDimension);
            const double scale = static_cast<double>(targetWidth) / static_cast<double>(srcWidth);
            targetHeight = std::max(1, static_cast<int>(std::round(static_cast<double>(srcHeight) * scale)));
        } else {
            targetHeight = static_cast<int>(kMaxDimension);
            const double scale = static_cast<double>(targetHeight) / static_cast<double>(srcHeight);
            targetWidth = std::max(1, static_cast<int>(std::round(static_cast<double>(srcWidth) * scale)));
        }
    }

    std::vector<std::uint8_t> finalData;
    if (targetWidth != srcWidth || targetHeight != srcHeight) {
        finalData = resampleBilinear(loadedImage.pixels.data(), srcWidth, srcHeight, targetWidth, targetHeight);
    } else {
        finalData = std::move(loadedImage.pixels);
    }

    if (finalData.empty()) {
        std::cerr << "Failed to process color map." << std::endl;
        resetState();
        return false;
    }

    m_width = targetWidth;
    m_height = targetHeight;
    m_colorData = std::move(finalData);
    m_isLoaded = true;
    std::cout << "Loaded color map (" << m_width << " x " << m_height << ")" << std::endl;
    return true;
}

std::vector<std::array<float, 3>> ColorMapSampler::sampleColorsForTerrain(double povLatDegrees, double povLonDegrees,
                                                                          int width, int height, float totalLatSpan,
                                                                          float totalLonSpan) {
    bool isLoaded = false;
    {
        std::scoped_lock lock(m_loadMutex);
        isLoaded = m_isLoaded;
    }

    if (!isLoaded && !load()) {
        if (width <= 0 || height <= 0) {
            return {};
        }

        const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
        return std::vector<std::array<float, 3>>(total, kFallbackColor);
    }

    if (width <= 0 || height <= 0) {
        return {};
    }

    std::vector<std::array<float, 3>> colorData;
    colorData.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));

    const float latN = 55.0f;
    const float latS = -55.0f;

    const float latTop = static_cast<float>(povLatDegrees) + (totalLatSpan / 2.0f);
    const float lonLeft = static_cast<float>(povLonDegrees) - (totalLonSpan / 2.0f);

    const float degPerPixelY = totalLatSpan / static_cast<float>(height);
    const float degPerPixelX = totalLonSpan / static_cast<float>(width);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float lat = latTop - (static_cast<float>(y) * degPerPixelY);
            const float lon = lonLeft + (static_cast<float>(x) * degPerPixelX);

            if (x == 0 && y == 0) {
                std::cout << "  Top-left lat,lon: (" << lat << ", " << lon << ")" << std::endl;
            }

            float u = (lon + 180.0f) / 360.0f;
            float v = (latN - lat) / (latN - latS);

            if (u >= 1.0f)
                u -= 1.0f;
            if (u < 0.0f)
                u += 1.0f;

            std::array<float, 3> color = kOutOfRangeColor;
            if (v >= 1.0f || v < 0.0f) {
                color = kOutOfRangeColor;
            } else {
                color = sample(u, v);
            }

            colorData.push_back(color);
        }
    }

    return colorData;
}

std::array<float, 3> ColorMapSampler::sample(float u, float v) {
    bool isLoaded = false;
    {
        std::scoped_lock lock(m_loadMutex);
        isLoaded = m_isLoaded;
    }

    if (!isLoaded && !load()) {
        return kFallbackColor;
    }

    std::scoped_lock lock(m_loadMutex);
    if (!m_isLoaded || m_colorData.empty() || m_width <= 0 || m_height <= 0) {
        return kFallbackColor;
    }

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    const float x = u * static_cast<float>(m_width - 1);
    const float y = v * static_cast<float>(m_height - 1);

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, m_width - 1);
    const int y1 = std::min(y0 + 1, m_height - 1);

    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const size_t idx00 = pixelIndex(x0, y0);
    const size_t idx10 = pixelIndex(x1, y0);
    const size_t idx01 = pixelIndex(x0, y1);
    const size_t idx11 = pixelIndex(x1, y1);

    const size_t maxIndex = m_colorData.size();
    if (idx00 + (kChannels - 1) >= maxIndex || idx10 + (kChannels - 1) >= maxIndex ||
        idx01 + (kChannels - 1) >= maxIndex || idx11 + (kChannels - 1) >= maxIndex) {
        return kFallbackColor;
    }

    constexpr float kInv255 = 1.0f / 255.0f;
    const auto fetchColor = [&](size_t index) {
        return std::array<float, 3>{m_colorData[index + 0] * kInv255, m_colorData[index + 1] * kInv255,
                                    m_colorData[index + 2] * kInv255};
    };

    const std::array<float, 3> c00 = fetchColor(idx00);
    const std::array<float, 3> c10 = fetchColor(idx10);
    const std::array<float, 3> c01 = fetchColor(idx01);
    const std::array<float, 3> c11 = fetchColor(idx11);

    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    std::array<float, 3> result{};
    for (int i = 0; i < 3; ++i) {
        const float a = lerp(c00[i], c10[i], tx);
        const float b = lerp(c01[i], c11[i], tx);
        result[i] = lerp(a, b, ty);
    }

    return result;
}

bool ColorMapSampler::hasData() const {
    std::scoped_lock lock(m_loadMutex);
    return m_isLoaded && !m_colorData.empty();
}

size_t ColorMapSampler::pixelIndex(int x, int y) const {
    return (static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)) * kChannels;
}
