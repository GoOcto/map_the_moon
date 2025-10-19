#pragma once

#include <vector>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <stdexcept>

class TerrainLoader {
public:
    static constexpr int MESH_SIZE = 1024;
    static constexpr int FULL_WIDTH = 23040;
    static constexpr int FULL_HEIGHT = 15360;
    
    static std::vector<float> loadLunarData(const char* filepath, int& outWidth, int& outHeight, 
                                           int xoffset = 0, int yoffset = 0, int steps = 1) {
        std::cout << "Loading lunar data from: " << filepath << std::endl;
        
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open file: " << filepath << std::endl;
            return {};
        }
        
        int centerX = FULL_WIDTH / 2 + xoffset;
        int centerY = FULL_HEIGHT / 2 + yoffset;
        int startX = centerX - MESH_SIZE / 2;
        int startY = centerY - MESH_SIZE / 2;

        // LINES                 = 15360
        // LINE_SAMPLES          = 23040
        std::cout << "Reading data block at (" << startX << ", " << startY << ") with step " << steps << std::endl;
        
        std::vector<float> data(MESH_SIZE * MESH_SIZE);

        // when offset is out of bounds of the file (15360 * 23040 * sizeof(float)), set the data to zero
        if (steps <= 0) {
            throw std::invalid_argument("TerrainLoader::loadLunarData requires steps > 0");
        }

        std::vector<float> rowBuffer(static_cast<size_t>(MESH_SIZE) * steps, -10.0f);

        for (int row = 0; row < MESH_SIZE; ++row) {
            std::fill(rowBuffer.begin(), rowBuffer.end(), -10.0f);

            const int srcY = startY + row * steps;
            if (srcY >= 0 && srcY < FULL_HEIGHT) {
                const int srcXStart = startX;
                const int srcXEnd = startX + MESH_SIZE * steps;
                const int validXStart = std::max(0, srcXStart);
                const int validXEnd = std::min(FULL_WIDTH, srcXEnd);

                if (validXStart < validXEnd) {
                    const size_t samplesToRead = static_cast<size_t>(validXEnd - validXStart);
                    const size_t bufferOffset = static_cast<size_t>(validXStart - srcXStart);

                    const std::int64_t linearIndex = static_cast<std::int64_t>(srcY) * FULL_WIDTH + validXStart;
                    const std::streamoff byteOffset = static_cast<std::streamoff>(linearIndex) * sizeof(float);

                    file.seekg(byteOffset);
                    file.read(reinterpret_cast<char*>(rowBuffer.data() + bufferOffset), samplesToRead * sizeof(float));

                    if (!file) {
                        // If the read failed or was partial, zero out the attempted range and clear stream state.
                        std::fill(rowBuffer.begin() + bufferOffset,
                                  rowBuffer.begin() + bufferOffset + samplesToRead,
                                  0.0f);
                        file.clear();
                    }
                }
            }

            for (int x = 0; x < MESH_SIZE; ++x) {
                const size_t sampleIndex = static_cast<size_t>(x * steps);
                const size_t dataIndex = static_cast<size_t>(row) * MESH_SIZE + x;
                data[dataIndex] = sampleIndex < rowBuffer.size() ? rowBuffer[sampleIndex] : 0.0f;
            }
        }
        
        file.close();
        
        // Convert from km to meters
        for (float& val : data) {
            val *= 1000.0f;
        }
        
        outWidth = MESH_SIZE;
        outHeight = MESH_SIZE;
        
        float minElev = *std::min_element(data.begin(), data.end());
        float maxElev = *std::max_element(data.begin(), data.end());
        std::cout << "Elevation range: " << minElev << " to " << maxElev << " meters" << std::endl;
        
        return data;
    }
    
    static void generateMesh(const std::vector<float>& elevationData, int width, int height,
                            std::vector<float>& vertices, std::vector<unsigned int>& indices) {
        std::cout << "Generating mesh..." << std::endl;
        
        float scaleZ = 0.01f;
        
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float elevation = elevationData[y * width + x];
                
                vertices.push_back(static_cast<float>(x));
                vertices.push_back(static_cast<float>(y));
                vertices.push_back(elevation * scaleZ);
                vertices.push_back(elevation);
            }
        }
        
        for (int y = 0; y < height - 1; y++) {
            for (int x = 0; x < width - 1; x++) {
                int topLeft = y * width + x;
                int topRight = topLeft + 1;
                int bottomLeft = (y + 1) * width + x;
                int bottomRight = bottomLeft + 1;
                
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);
                
                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }
        
        std::cout << "Generated " << vertices.size() / 4 << " vertices and " 
                  << indices.size() / 3 << " triangles" << std::endl;
    }
    
    static void updateMeshElevations(const std::vector<float>& elevationData, int width, int height,
                                    std::vector<float>& vertices) {
        float scaleZ = 0.01f;
        
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int vertexIndex = (y * width + x) * 4;
                float elevation = elevationData[y * width + x];
                
                vertices[vertexIndex + 2] = elevation * scaleZ;
                vertices[vertexIndex + 3] = elevation;
            }
        }
    }
};
