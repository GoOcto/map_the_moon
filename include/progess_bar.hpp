#pragma once

#include <GL/glew.h>

#include "shader.hpp"

#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>

class ProgressBarOverlay {
  public:
    ProgressBarOverlay() = default;
    ~ProgressBarOverlay() {
        if (vbo_ != 0) {
            glDeleteBuffers(1, &vbo_);
        }
        if (vao_ != 0) {
            glDeleteVertexArrays(1, &vao_);
        }
    }

    void initialize() {
        if (!shader_) {
            static constexpr const char* kVertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
uniform mat4 uProjection;
uniform vec2 uOffset;
uniform vec2 uSize;
void main() {
    vec2 scaled = uOffset + aPos * uSize;
    gl_Position = uProjection * vec4(scaled, 0.0, 1.0);
}
)";

            static constexpr const char* kFragmentShader = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
void main() {
    FragColor = uColor;
}
)";

            shader_ = std::make_unique<ShaderProgram>(kVertexShader, kFragmentShader);
            projectionLoc_ = shader_->getUniformLocation("uProjection");
            offsetLoc_ = shader_->getUniformLocation("uOffset");
            sizeLoc_ = shader_->getUniformLocation("uSize");
            colorLoc_ = shader_->getUniformLocation("uColor");
        }

        if (vao_ == 0) {
            glGenVertexArrays(1, &vao_);
        }
        if (vbo_ == 0) {
            glGenBuffers(1, &vbo_);
        }

        static constexpr float quadVertices[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f,
        };

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void setScreenSize(const glm::vec2& size) {
        screenSize_ = size;
    }

    void render(float progress) {
        if (!shader_ || screenSize_.x <= 0.0f || screenSize_.y <= 0.0f) {
            return;
        }

        const float clamped = std::clamp(progress, 0.0f, 1.0f);
        const GLboolean wasDepthEnabled = glIsEnabled(GL_DEPTH_TEST);
        const GLboolean wasCullEnabled = glIsEnabled(GL_CULL_FACE);
        const GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
        std::array<GLint, 2> previousPolygonMode{GL_FILL, GL_FILL};
        glGetIntegerv(GL_POLYGON_MODE, previousPolygonMode.data());

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        const glm::mat4 projection = glm::ortho(0.0f, screenSize_.x, screenSize_.y, 0.0f);

        shader_->use();
        glBindVertexArray(vao_);
        glUniformMatrix4fv(projectionLoc_, 1, GL_FALSE, glm::value_ptr(projection));

        auto drawQuad = [&](const glm::vec2& offset, const glm::vec2& size, const glm::vec4& color) {
            glUniform2f(offsetLoc_, offset.x, offset.y);
            glUniform2f(sizeLoc_, size.x, size.y);
            glUniform4f(colorLoc_, color.r, color.g, color.b, color.a);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        };

        const float barWidth = std::min(screenSize_.x * 0.5f, 420.0f);
        const float barHeight = 28.0f;
        const glm::vec2 barOrigin{(screenSize_.x - barWidth) * 0.5f, (screenSize_.y - barHeight) * 0.5f};

        drawQuad(glm::vec2{0.0f, 0.0f}, screenSize_, glm::vec4{0.0f, 0.0f, 0.0f, 0.55f});
        drawQuad(barOrigin, glm::vec2{barWidth, barHeight}, glm::vec4{0.15f, 0.17f, 0.24f, 0.95f});

        static constexpr float kPadding = 4.0f;
        const float filledWidth = (barWidth - kPadding * 2.0f) * clamped;
        if (filledWidth > 0.5f) {
            const glm::vec2 fillOrigin{barOrigin.x + kPadding, barOrigin.y + kPadding};
            drawQuad(fillOrigin, glm::vec2{filledWidth, barHeight - kPadding * 2.0f},
                     glm::vec4{0.35f, 0.65f, 0.98f, 1.0f});
        }

        glBindVertexArray(0);

        if (!wasBlendEnabled) {
            glDisable(GL_BLEND);
        }
        if (wasDepthEnabled) {
            glEnable(GL_DEPTH_TEST);
        }
        if (wasCullEnabled) {
            glEnable(GL_CULL_FACE);
        }
        glPolygonMode(GL_FRONT, previousPolygonMode[0]);
        glPolygonMode(GL_BACK, previousPolygonMode[1]);
    }

  private:
    std::unique_ptr<ShaderProgram> shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLint projectionLoc_ = -1;
    GLint offsetLoc_ = -1;
    GLint sizeLoc_ = -1;
    GLint colorLoc_ = -1;
    glm::vec2 screenSize_{1.0f, 1.0f};
};
