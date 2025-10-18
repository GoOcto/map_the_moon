#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <iostream>

class Mesh {
public:
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint EBO = 0;
    
    Mesh() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);
    }
    
    ~Mesh() {
        if (VAO != 0) glDeleteVertexArrays(1, &VAO);
        if (VBO != 0) glDeleteBuffers(1, &VBO);
        if (EBO != 0) glDeleteBuffers(1, &EBO);
    }
    
    void bind() const {
        glBindVertexArray(VAO);
    }
    
    void setupVertexAttributes(const std::vector<int>& attributeSizes) {
        bind();
        
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        
        int stride = 0;
        for (int size : attributeSizes) {
            stride += size;
        }
        stride *= sizeof(float);
        
        int offset = 0;
        for (GLuint i = 0; i < static_cast<GLuint>(attributeSizes.size()); ++i) {
            glVertexAttribPointer(i, attributeSizes[i], GL_FLOAT, GL_FALSE, 
                                 stride, (void*)(offset * sizeof(float)));
            glEnableVertexAttribArray(i);
            offset += attributeSizes[i];
        }
    }
    
    void uploadData() {
        bind();
        
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), 
                     vertices.data(), GL_STATIC_DRAW);
        
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
                     indices.data(), GL_STATIC_DRAW);
    }
    
    void updateVertexData() {
        bind();
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(float), vertices.data());
    }
    
    void draw() const {
        bind();
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), 
                      GL_UNSIGNED_INT, nullptr);
    }
    
    size_t getIndexCount() const {
        return indices.size();
    }
};
