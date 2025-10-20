#include "color_map_sampler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace {
std::once_flag gLoadFlag;
bool gLoaded = false;
int gWidth = 0;
int gHeight = 0;
std::vector<std::uint8_t> gColorData;
std::filesystem::path gDataRoot{"."};

constexpr const char* kColorMapPath = ".data/color/colormap.jpg";
constexpr UINT kMaxDimension = 4096;
constexpr size_t kChannels = 3;

size_t pixelIndex(int x, int y) {
    return (static_cast<size_t>(y) * static_cast<size_t>(gWidth) + static_cast<size_t>(x)) * kChannels;
}
}

void ColorMapSampler::setDataRoot(std::string dataRoot) {
    if (dataRoot.empty()) {
        gDataRoot = std::filesystem::path{"."};
    } else {
        gDataRoot = std::filesystem::path(std::move(dataRoot));
    }
}

bool ColorMapSampler::ensureLoaded() {
    std::call_once(gLoadFlag, []() {
        const std::filesystem::path path = gDataRoot / kColorMapPath;
        if (!std::filesystem::exists(path)) {
            std::cerr << "Color map not found at " << path.string() << std::endl;
            gLoaded = false;
            return;
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
            gLoaded = false;
            return;
        }

        CoUninitGuard coGuard{needUninitialize};

        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            std::cerr << "Failed to create WIC factory. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
            gLoaded = false;
            return;
        }

        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        const std::wstring widePath = path.wstring();
        hr = factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                &decoder);
        if (FAILED(hr)) {
            std::cerr << "Failed to decode color map: " << path.string() << " HRESULT=0x" << std::hex << hr
                      << std::dec << std::endl;
            gLoaded = false;
            return;
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) {
            std::cerr << "Failed to obtain frame from color map. HRESULT=0x" << std::hex << hr << std::dec
                      << std::endl;
            gLoaded = false;
            return;
        }

        Microsoft::WRL::ComPtr<IWICBitmapSource> source;
        hr = frame.As(&source);
        if (FAILED(hr)) {
            std::cerr << "Failed to query IWICBitmapSource from frame. HRESULT=0x" << std::hex << hr << std::dec
                      << std::endl;
            gLoaded = false;
            return;
        }

        UINT width = 0;
        UINT height = 0;
        hr = source->GetSize(&width, &height);
        if (FAILED(hr) || width == 0 || height == 0) {
            std::cerr << "Invalid dimensions for color map." << std::endl;
            gLoaded = false;
            return;
        }

        UINT targetWidth = width;
        UINT targetHeight = height;
        if (targetWidth > kMaxDimension || targetHeight > kMaxDimension) {
            if (targetWidth >= targetHeight) {
                targetWidth = kMaxDimension;
                targetHeight = static_cast<UINT>(std::max(1.0,
                                                          std::round(static_cast<double>(height) * targetWidth /
                                                                     static_cast<double>(width))));
            } else {
                targetHeight = kMaxDimension;
                targetWidth = static_cast<UINT>(std::max(1.0,
                                                         std::round(static_cast<double>(width) * targetHeight /
                                                                    static_cast<double>(height))));
            }
        }

        Microsoft::WRL::ComPtr<IWICBitmapSource> workingSource = source;
        if (targetWidth != width || targetHeight != height) {
            Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
            hr = factory->CreateBitmapScaler(&scaler);
            if (FAILED(hr)) {
                std::cerr << "Failed to create WIC bitmap scaler. HRESULT=0x" << std::hex << hr << std::dec
                          << std::endl;
                gLoaded = false;
                return;
            }

            hr = scaler->Initialize(workingSource.Get(), targetWidth, targetHeight, WICBitmapInterpolationModeFant);
            if (FAILED(hr)) {
                std::cerr << "Failed to scale color map. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
                gLoaded = false;
                return;
            }

            workingSource = scaler;
            width = targetWidth;
            height = targetHeight;
        }

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) {
            std::cerr << "Failed to create WIC format converter. HRESULT=0x" << std::hex << hr << std::dec
                      << std::endl;
            gLoaded = false;
            return;
        }

        hr = converter->Initialize(workingSource.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr,
                                   0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            std::cerr << "Failed to initialize WIC converter. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
            gLoaded = false;
            return;
        }

        Microsoft::WRL::ComPtr<IWICBitmapSource> convertedSource;
        hr = converter.As(&convertedSource);
        if (FAILED(hr)) {
            std::cerr << "Failed to query converted bitmap source. HRESULT=0x" << std::hex << hr << std::dec
                      << std::endl;
            gLoaded = false;
            return;
        }

        const UINT stride = width * static_cast<UINT>(kChannels);
        const UINT bufferSize = stride * height;
        std::vector<BYTE> buffer(bufferSize);
        hr = convertedSource->CopyPixels(nullptr, stride, bufferSize, buffer.data());
        if (FAILED(hr)) {
            std::cerr << "Failed to copy pixels from color map. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
            gLoaded = false;
            return;
        }

        gWidth = static_cast<int>(width);
        gHeight = static_cast<int>(height);
        gColorData.resize(static_cast<size_t>(gWidth) * static_cast<size_t>(gHeight) * kChannels);
        for (UINT y = 0; y < height; ++y) {
            const BYTE* srcRow = buffer.data() + static_cast<size_t>(y) * stride;
            for (UINT x = 0; x < width; ++x) {
                const size_t srcIndex = static_cast<size_t>(x) * kChannels;
                const size_t dstIndex = pixelIndex(static_cast<int>(x), static_cast<int>(y));
                gColorData[dstIndex + 0] = srcRow[srcIndex + 2];
                gColorData[dstIndex + 1] = srcRow[srcIndex + 1];
                gColorData[dstIndex + 2] = srcRow[srcIndex + 0];
            }
        }

        gLoaded = true;
        std::cout << "Loaded color map (" << gWidth << " x " << gHeight << ")" << std::endl;
#else
        std::cerr << "Color map loading is currently supported on Windows only." << std::endl;
        gLoaded = false;
#endif
    });

    return gLoaded;
}

std::array<float, 3> ColorMapSampler::sample(float u, float v) {
    if (!hasData() && !ensureLoaded()) {
        return {1.0f, 1.0f, 1.0f};
    }

    if (!gLoaded || gColorData.empty() || gWidth <= 0 || gHeight <= 0) {
        return {1.0f, 1.0f, 1.0f};
    }

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    const float x = u * static_cast<float>(gWidth - 1);
    const float y = v * static_cast<float>(gHeight - 1);

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, gWidth - 1);
    const int y1 = std::min(y0 + 1, gHeight - 1);

    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const size_t idx00 = pixelIndex(x0, y0);
    const size_t idx10 = pixelIndex(x1, y0);
    const size_t idx01 = pixelIndex(x0, y1);
    const size_t idx11 = pixelIndex(x1, y1);

    const size_t maxIndex = gColorData.size();
    if (idx00 + (kChannels - 1) >= maxIndex || idx10 + (kChannels - 1) >= maxIndex ||
        idx01 + (kChannels - 1) >= maxIndex || idx11 + (kChannels - 1) >= maxIndex) {
        return {1.0f, 1.0f, 1.0f};
    }

    constexpr float kInv255 = 1.0f / 255.0f;
    const auto fetchColor = [&](size_t index) {
        return std::array<float, 3>{gColorData[index + 0] * kInv255, gColorData[index + 1] * kInv255,
                                    gColorData[index + 2] * kInv255};
    };

    std::array<float, 3> c00 = fetchColor(idx00);
    std::array<float, 3> c10 = fetchColor(idx10);
    std::array<float, 3> c01 = fetchColor(idx01);
    std::array<float, 3> c11 = fetchColor(idx11);

    const auto lerp = [](float a, float b, float t) {
        return a + (b - a) * t;
    };

    std::array<float, 3> result{};
    for (int i = 0; i < 3; ++i) {
        const float a = lerp(c00[i], c10[i], tx);
        const float b = lerp(c01[i], c11[i], tx);
        result[i] = lerp(a, b, ty);
    }

    return result;
}

bool ColorMapSampler::hasData() {
    return gLoaded && !gColorData.empty();
}
