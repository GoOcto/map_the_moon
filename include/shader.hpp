#pragma once

#include <GL/glew.h>
#include <iostream>
#include <string>

class ShaderProgram {
private:
    GLuint programId = 0;
    
    GLuint compileShader(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            std::cerr << "Shader compilation failed:\n" << infoLog << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        
        return shader;
    }
    
public:
    ShaderProgram(const char* vertexSource, const char* fragmentSource) {
        GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
        GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
        
        if (vertexShader == 0 || fragmentShader == 0) {
            throw std::runtime_error("Failed to compile shaders");
        }
        
        programId = glCreateProgram();
        glAttachShader(programId, vertexShader);
        glAttachShader(programId, fragmentShader);
        glLinkProgram(programId);
        
        GLint success;
        glGetProgramiv(programId, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(programId, 512, nullptr, infoLog);
            std::cerr << "Program linking failed:\n" << infoLog << std::endl;
            glDeleteProgram(programId);
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            throw std::runtime_error("Failed to link shader program");
        }
        
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }
    
    ~ShaderProgram() {
        if (programId != 0) {
            glDeleteProgram(programId);
        }
    }
    
    void use() const {
        glUseProgram(programId);
    }
    
    GLint getUniformLocation(const char* name) const {
        return glGetUniformLocation(programId, name);
    }
    
    GLuint getId() const {
        return programId;
    }
};
