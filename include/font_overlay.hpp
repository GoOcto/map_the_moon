#ifndef FONT_OVERLAY_HPP
#define FONT_OVERLAY_HPP

#include <GL/glew.h>

#include "shader.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef FONT_OVERLAY_STB_IMPLEMENTATION
#define FONT_OVERLAY_STB_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#endif

class FontOverlay {
public:
    FontOverlay() = default;

    ~FontOverlay() {
        if (overlayVbo_ != 0) glDeleteBuffers(1, &overlayVbo_);
        if (overlayVao_ != 0) glDeleteVertexArrays(1, &overlayVao_);
        if (fontTexture_ != 0) glDeleteTextures(1, &fontTexture_);
    }

    void initialize(const std::string& fontPath) {
        fontTexture_ = 0;
        overlayVao_ = 0;
        overlayVbo_ = 0;
        overlayVertexCount_ = 0;
        fpsText_ = "FPS 0";

        loadFont(fontPath);
        createTexture();
        createShader();
        createBuffers();
        rebuildOverlayGeometry();
    }

    void setScreenSize(const glm::vec2& size) {
        screenSize_ = size;
    }

    void update(float deltaTime) {
        if (!overlayShader_) return;

        fpsAccumulator_ += deltaTime;
        ++frameCount_;

        if (fpsAccumulator_ < kOverlayUpdateInterval) return;

        currentFps_ = (fpsAccumulator_ > 0.0f) ? static_cast<float>(frameCount_) / fpsAccumulator_ : 0.0f;
        frameCount_ = 0;
        fpsAccumulator_ = 0.0f;

        std::ostringstream oss;
        oss << "FPS " << std::fixed << std::setprecision(0) << currentFps_;
        fpsText_ = oss.str();
        rebuildOverlayGeometry();
    }

    void render() {
        if (!overlayShader_ || overlayVertexCount_ == 0) return;

        std::array<GLint, 2> prevPolygonMode{GL_FILL, GL_FILL};
        glGetIntegerv(GL_POLYGON_MODE, prevPolygonMode.data());
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        const GLboolean wasCullEnabled = glIsEnabled(GL_CULL_FACE);
        const GLboolean wasDepthEnabled = glIsEnabled(GL_DEPTH_TEST);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        overlayShader_->use();

        glm::mat4 projection = glm::ortho(0.0f, screenSize_.x, screenSize_.y, 0.0f);
        glUniformMatrix4fv(overlayProjectionLoc_, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(overlayTextColorLoc_, 1.0f, 1.0f, 1.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fontTexture_);
        glUniform1i(overlayTextureLoc_, 0);

        glBindVertexArray(overlayVao_);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(overlayVertexCount_));
        glBindVertexArray(0);

        glDisable(GL_BLEND);
        if (wasDepthEnabled) {
            glEnable(GL_DEPTH_TEST);
        }
        if (wasCullEnabled) {
            glEnable(GL_CULL_FACE);
        }
        glPolygonMode(GL_FRONT, prevPolygonMode[0]);
        glPolygonMode(GL_BACK, prevPolygonMode[1]);
    }

private:
    static constexpr float kOverlayUpdateInterval = 0.25f;
    static constexpr float kOverlayPadding = 10.0f;
    static constexpr float kFontSize = 15.0f;
    static constexpr int kFontAtlasWidth = 512;
    static constexpr int kFontAtlasHeight = 512;

    void loadFont(const std::string& fontPath) {
        std::ifstream fontFile(fontPath, std::ios::binary | std::ios::ate);
        if (!fontFile.is_open()) {
            throw std::runtime_error("Failed to open font file: " + fontPath);
        }

        std::streamsize size = fontFile.tellg();
        fontFile.seekg(0, std::ios::beg);

        fontBuffer_.resize(static_cast<size_t>(size));
        if (!fontFile.read(fontBuffer_.data(), size)) {
            throw std::runtime_error("Failed to read font file.");
        }
        fontFile.close();

        fontBitmap_.assign(kFontAtlasWidth * kFontAtlasHeight, 0);
        stbtt_BakeFontBitmap(reinterpret_cast<const unsigned char*>(fontBuffer_.data()), 0, kFontSize,
                             fontBitmap_.data(), kFontAtlasWidth, kFontAtlasHeight, 32, 96, cdata_);
    }

    void createTexture() {
        glGenTextures(1, &fontTexture_);
        glBindTexture(GL_TEXTURE_2D, fontTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, kFontAtlasWidth, kFontAtlasHeight, 0,
                     GL_RED, GL_UNSIGNED_BYTE, fontBitmap_.data());
    }

    void createShader() {
        overlayShader_ = std::make_unique<ShaderProgram>(kOverlayVertexShader, kOverlayFragmentShader);
        overlayProjectionLoc_ = overlayShader_->getUniformLocation("uProjection");
        overlayTextColorLoc_ = overlayShader_->getUniformLocation("uTextColor");
        overlayTextureLoc_ = overlayShader_->getUniformLocation("uText");
    }

    void createBuffers() {
        glGenVertexArrays(1, &overlayVao_);
        glGenBuffers(1, &overlayVbo_);
        glBindVertexArray(overlayVao_);
        glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4 * 64, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void rebuildOverlayGeometry() {
        if (!overlayShader_) return;

        std::vector<float> vertices;
        vertices.reserve(fpsText_.length() * 6 * 4);

        float x = kOverlayPadding;
        float y = kOverlayPadding;

        for (char c : fpsText_) {
            if (c < 32 || c >= 128) continue;

            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata_, kFontAtlasWidth, kFontAtlasHeight, c - 32, &x, &y, &q, 1);

            const float y0 = q.y0 + kFontSize + 5.0f;
            const float y1 = q.y1 + kFontSize + 5.0f;

            float quadVerts[] = {
                q.x0, y1, q.s0, q.t1,
                q.x0, y0, q.s0, q.t0,
                q.x1, y0, q.s1, q.t0,

                q.x0, y1, q.s0, q.t1,
                q.x1, y0, q.s1, q.t0,
                q.x1, y1, q.s1, q.t1,
            };
            vertices.insert(vertices.end(), std::begin(quadVerts), std::end(quadVerts));
        }

        overlayVertexCount_ = vertices.size() / 4;

        glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    static constexpr const char* kOverlayVertexShader = R"(
#version 330 core
layout (location = 0) in vec4 aVertex;
out vec2 vTexCoord;
uniform mat4 uProjection;
void main() {
    gl_Position = uProjection * vec4(aVertex.xy, 0.0, 1.0);
    vTexCoord = aVertex.zw;
}
)";

    static constexpr const char* kOverlayFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D uText;
uniform vec3 uTextColor;
void main() {
    float alpha = texture(uText, vTexCoord).r;
    FragColor = vec4(uTextColor, alpha);
}
)";

    std::unique_ptr<ShaderProgram> overlayShader_;
    GLuint fontTexture_ = 0;
    stbtt_bakedchar cdata_[96];
    GLuint overlayVao_ = 0;
    GLuint overlayVbo_ = 0;
    GLint overlayProjectionLoc_ = -1;
    GLint overlayTextColorLoc_ = -1;
    GLint overlayTextureLoc_ = -1;
    size_t overlayVertexCount_ = 0;

    glm::vec2 screenSize_{0.0f, 0.0f};

    float fpsAccumulator_ = 0.0f;
    int frameCount_ = 0;
    float currentFps_ = 0.0f;
    std::string fpsText_ = "FPS 0";

    std::vector<char> fontBuffer_;
    std::vector<unsigned char> fontBitmap_;
};

#endif
