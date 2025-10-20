#include "color_map_sampler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace {
constexpr char kColorMapPath[] = ".data/color/colormap.jpg";
constexpr unsigned int kMaxDimension = 4096;
constexpr size_t kChannels = 3;
constexpr std::array<float, 3> kFallbackColor{1.0f, 1.0f, 1.0f};
constexpr std::array<float, 3> kOutOfRangeColor{0.7f, 0.7f, 0.7f};
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

#ifdef _WIN32
    struct CoUninitGuard {
        bool active = false;
        ~CoUninitGuard() {
            if (active) {
                CoUninitialize();
            }
        }
    };

    bool needUninitialize = false;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == S_OK || hr == S_FALSE) {
        needUninitialize = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        // Already initialized with a different concurrency model; proceed without uninitializing.
    } else if (FAILED(hr)) {
        std::cerr << "CoInitializeEx failed with HRESULT=0x" << std::hex << hr << std::dec << std::endl;
        resetState();
        return false;
    }

    CoUninitGuard coGuard{needUninitialize};

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        std::cerr << "Failed to create WIC factory. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
        resetState();
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    const std::wstring widePath = path.wstring();
    hr = factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                            &decoder);
    if (FAILED(hr)) {
        std::cerr << "Failed to decode color map: " << path.string() << " HRESULT=0x" << std::hex << hr << std::dec
                  << std::endl;
        resetState();
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        std::cerr << "Failed to obtain frame from color map. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
        resetState();
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapSource> source;
    hr = frame.As(&source);
    if (FAILED(hr)) {
        std::cerr << "Failed to query IWICBitmapSource from frame. HRESULT=0x" << std::hex << hr << std::dec
                  << std::endl;
        resetState();
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    hr = source->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        std::cerr << "Invalid dimensions for color map." << std::endl;
        resetState();
        return false;
    }

    UINT targetWidth = width;
    UINT targetHeight = height;
    if (targetWidth > kMaxDimension || targetHeight > kMaxDimension) {
        if (targetWidth >= targetHeight) {
            targetWidth = kMaxDimension;
            targetHeight = static_cast<UINT>(
                std::max(1.0, std::round(static_cast<double>(height) * targetWidth / static_cast<double>(width))));
        } else {
            targetHeight = kMaxDimension;
            targetWidth = static_cast<UINT>(
                std::max(1.0, std::round(static_cast<double>(width) * targetHeight / static_cast<double>(height))));
        }
    }

    Microsoft::WRL::ComPtr<IWICBitmapSource> workingSource = source;
    if (targetWidth != width || targetHeight != height) {
        Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
        hr = factory->CreateBitmapScaler(&scaler);
        if (FAILED(hr)) {
            std::cerr << "Failed to create WIC bitmap scaler. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
            resetState();
            return false;
        }

        hr = scaler->Initialize(workingSource.Get(), targetWidth, targetHeight, WICBitmapInterpolationModeFant);
        if (FAILED(hr)) {
            std::cerr << "Failed to scale color map. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
            resetState();
            return false;
        }

        workingSource = scaler;
        width = targetWidth;
        height = targetHeight;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        std::cerr << "Failed to create WIC format converter. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
        resetState();
        return false;
    }

    hr = converter->Initialize(workingSource.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize WIC converter. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
        resetState();
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapSource> convertedSource;
    hr = converter.As(&convertedSource);
    if (FAILED(hr)) {
        std::cerr << "Failed to query converted bitmap source. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
        resetState();
        return false;
    }

    const UINT stride = width * static_cast<UINT>(kChannels);
    const UINT bufferSize = stride * height;
    std::vector<BYTE> buffer(bufferSize);
    hr = convertedSource->CopyPixels(nullptr, stride, bufferSize, buffer.data());
    if (FAILED(hr)) {
        std::cerr << "Failed to copy pixels from color map. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
        resetState();
        return false;
    }

    m_width = static_cast<int>(width);
    m_height = static_cast<int>(height);
    m_colorData.resize(static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * kChannels);
    for (UINT y = 0; y < height; ++y) {
        const BYTE* srcRow = buffer.data() + static_cast<size_t>(y) * stride;
        for (UINT x = 0; x < width; ++x) {
            const size_t srcIndex = static_cast<size_t>(x) * kChannels;
            const size_t dstIndex = pixelIndex(static_cast<int>(x), static_cast<int>(y));
            m_colorData[dstIndex + 0] = srcRow[srcIndex + 2];
            m_colorData[dstIndex + 1] = srcRow[srcIndex + 1];
            m_colorData[dstIndex + 2] = srcRow[srcIndex + 0];
        }
    }

    m_isLoaded = true;
    std::cout << "Loaded color map (" << m_width << " x " << m_height << ")" << std::endl;
    return true;
#else
    std::cerr << "Color map loading is currently supported on Windows only." << std::endl;
    resetState();
    return false;
#endif
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
