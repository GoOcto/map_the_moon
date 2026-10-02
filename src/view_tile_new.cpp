/*
 * Lunar Surface Viewer — OpenEXR Multi-Resolution Pyramid Edition
 *
 * Behaviour differences vs view_tile:
 *   - Data loaded from per-LOD OpenEXR files (*_lod0.exr … *_lod6.exr)
 *   - LOD 0 = 23040x15360 (512 ppd), LOD 6 = 360x240 (8 ppd)
 *   - Keys 0-6 / [ / ] / scroll switch pyramid level instantly
 *   - Each level is cached in RAM after first load
 */

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "application.hpp"
#include "color_map_sampler.hpp"
#include "font_overlay.hpp"
#include "mesh.hpp"
#include "shader.hpp"
#include "terrain_dataset.hpp"
#include "terrain_loader_exr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Camera constants
constexpr float  kMinDistance       = 10.0f;
constexpr float  kMaxDistance       = 2000.0f;
constexpr float  kShiftSpeed        = 150.0f;
constexpr float  kNormalSpeed       = 50.0f;
constexpr float  kLightAngle[]      = { 20.0f, 45.0f, 70.0f };
constexpr double kDefaultLat        = 15.0;
constexpr double kDefaultLon        = 22.5;
constexpr double kLatStep           = 0.2;
constexpr double kLonStep           = 0.2;
constexpr double kMinLat            = 0.0;
constexpr double kMaxLat            = 30.0;

double wrapLon(double lon) {
    double w = std::fmod(lon, 360.0);
    return w < 0.0 ? w + 360.0 : w;
}

// -----------------------------------------------------------------------
const char* kVS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aElev;
layout(location=2) in vec3 aColor;

out float elevation;
out vec3  FragPos;
out vec3  WorldPos;
out vec3  vertexColor;

uniform mat4  model;
uniform mat4  view;
uniform mat4  projection;
uniform float uCurvature;
uniform vec2  uMeshCenter;
uniform vec2  vNWCorner;
uniform vec2  vSECorner;
uniform uint  dimensions;

void main(){
    const float eps = 1e-6;
    vec2 c = vec2(aPos.x - uMeshCenter.x, aPos.y - uMeshCenter.y);
    vec3 curved = vec3(c, aPos.z);

    float curv = radians(vNWCorner.x - vSECorner.x) / float(dimensions);
    if(abs(curv) > eps){
        float r = 1.0 / curv;
        float tx = c.x * curv, ty = c.y * curv;
        curved.x = r * sin(tx);
        curved.y = r * sin(ty);
        curved.z = aPos.z - r * (2.0 - cos(tx) - cos(ty));
    }

    vec4 wp  = model * vec4(curved, 1.0);
    WorldPos = wp.xyz;
    FragPos  = vec3(view * wp);
    elevation   = aElev;
    vertexColor = aColor;
    gl_Position = projection * view * wp;
}
)";

const char* kFS = R"(
#version 330 core
in float elevation;
in vec3  FragPos;
in vec3  WorldPos;
in vec3  vertexColor;
out vec4 FragColor;

uniform float minElevation;
uniform float maxElevation;
uniform vec3  lightDirection;
uniform float colorMode;   // 0=relief 1=vertex

vec3 reliefColor(float n){
    vec3 c[5];
    c[0]=vec3(0.1,0.2,0.5); c[1]=vec3(0.3,0.5,0.3);
    c[2]=vec3(0.6,0.5,0.3); c[3]=vec3(0.8,0.8,0.7);
    c[4]=vec3(1.0,1.0,1.0);
    float s = clamp(n,0.0,1.0)*4.0;
    int   i = clamp(int(floor(s)),0,3);
    return mix(c[i],c[i+1],s-float(i))*0.7 + vec3(0.3);
}

void main(){
    float range = max(maxElevation-minElevation, 1e-4);
    float n     = clamp((elevation-minElevation)/range, 0.0, 1.0);
    vec3  base  = mix(vertexColor, reliefColor(n), colorMode);

    vec3 N = normalize(cross(dFdx(WorldPos), dFdy(WorldPos)));
    vec3 L = normalize(lightDirection);
    float diff = max(dot(N,L),0.0);
    vec3  H    = normalize(L + normalize(-FragPos));
    float spec = pow(max(dot(N,H),0.0),32.0)*0.3;

    FragColor = vec4(base*(0.25 + diff*0.75 + spec), 1.0);
}
)";

} // namespace

// ===========================================================================
class LunarViewerEXR : public Application {
public:
    LunarViewerEXR(const char* title, std::string dataRoot)
        : Application(title), dataRoot_(std::move(dataRoot))
    {
        colorSampler_ = std::make_unique<ColorMapSampler>(dataRoot_);
        exrLoader_    = std::make_unique<TerrainLoaderEXR>();
    }

protected:
    // -----------------------------------------------------------------------
    void setup() override {
        setupCallbacks();
        shader_ = std::make_unique<ShaderProgram>(kVS, kFS);

        loadTerrain();
        mesh->uploadData();
        mesh->setupVertexAttributes({3, 1, 3});

        shader_->use();
        uModel_    = shader_->getUniformLocation("model");
        uView_     = shader_->getUniformLocation("view");
        uProj_     = shader_->getUniformLocation("projection");
        uMinElev_  = shader_->getUniformLocation("minElevation");
        uMaxElev_  = shader_->getUniformLocation("maxElevation");
        uLight_    = shader_->getUniformLocation("lightDirection");
        uColorMode_= shader_->getUniformLocation("colorMode");
        uCurv_     = shader_->getUniformLocation("uCurvature");
        uCenter_   = shader_->getUniformLocation("uMeshCenter");
        uNW_       = shader_->getUniformLocation("vNWCorner");
        uSE_       = shader_->getUniformLocation("vSECorner");
        uDims_     = shader_->getUniformLocation("dimensions");

        screenSize_ = {(float)window->currentWidth, (float)window->currentHeight};
        fpsOverlay_.initialize(dataRoot_ + "fonts/ProggyClean.ttf");
        fpsOverlay_.setScreenSize(screenSize_);

        lightDir_ = glm::normalize(glm::vec3(
            glm::radians(kLightAngle[0]),
            glm::radians(kLightAngle[1]),
            glm::radians(kLightAngle[2])));

        centerCamera();
        printHelp();
    }

    void update(float dt) override {
        fpsOverlay_.update(dt);
        if (needsReload_) reloadTerrain();
    }

    void render() override {
        shader_->use();
        glUniformMatrix4fv(uModel_, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.f)));
        glUniformMatrix4fv(uView_,  1, GL_FALSE, glm::value_ptr(getViewMatrix()));
        glUniformMatrix4fv(uProj_,  1, GL_FALSE, glm::value_ptr(getProjectionMatrix()));

        glUniform1f(uMinElev_,   minElev_);
        glUniform1f(uMaxElev_,   maxElev_);
        glUniform1f(uColorMode_, colorMode_);
        glUniform3fv(uLight_,  1, glm::value_ptr(lightDir_));
        glUniform1f(uCurv_,    curvature_);
        glUniform2fv(uCenter_, 1, glm::value_ptr(glm::vec2(width_/2.f, height_/2.f)));
        glUniform2fv(uNW_,     1, glm::value_ptr(nwCorner_));
        glUniform2fv(uSE_,     1, glm::value_ptr(seCorner_));
        glUniform1ui(uDims_,   static_cast<GLuint>(width_));

        mesh->draw();
        fpsOverlay_.render();
    }

    // -----------------------------------------------------------------------
    void keyCallback(GLFWwindow* w, int key, int sc, int action, int mods) override {
        Application::keyCallback(w, key, sc, action, mods);
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

        switch (key) {
        // LOD via digit keys
        case GLFW_KEY_0: case GLFW_KEY_1: case GLFW_KEY_2:
        case GLFW_KEY_3: case GLFW_KEY_4: case GLFW_KEY_5: case GLFW_KEY_6:
            setLOD(key - GLFW_KEY_0); break;

        // LOD coarser / finer
        case GLFW_KEY_LEFT_BRACKET:  case GLFW_KEY_MINUS:  setLOD(lod_ + 1); break;
        case GLFW_KEY_RIGHT_BRACKET: case GLFW_KEY_EQUAL:  setLOD(lod_ - 1); break;

        // Pan — step size scales with LOD so movement feels consistent
        case GLFW_KEY_UP:    case GLFW_KEY_W: adjustLat( kLatStep * (1 << lod_)); break;
        case GLFW_KEY_DOWN:  case GLFW_KEY_S: adjustLat(-kLatStep * (1 << lod_)); break;
        case GLFW_KEY_LEFT:  case GLFW_KEY_A: adjustLon(-kLonStep * (1 << lod_)); break;
        case GLFW_KEY_RIGHT: case GLFW_KEY_D: adjustLon( kLonStep * (1 << lod_)); break;

        case GLFW_KEY_C: // toggle colour mode
            colorMode_ = (colorMode_ == 0.f) ? 1.f : 0.f;
            std::cout << "Color mode: " << (colorMode_==0.f ? "Relief" : "Colormap") << "\n";
            break;
        case GLFW_KEY_R:
            centerCamera(); break;
        }
    }

    void mouseCallback(GLFWwindow* w, double x, double y) override {
        const glm::vec2 d = input->getMouseDelta(x, y);
        if (input->leftMousePressed) {
            camera->yaw   -= d.x * camera->sensitivity;
            camera->pitch -= d.y * camera->sensitivity;
            camera->constrainPitch();
            camera->updateVectors();
        }
        if (input->rightMousePressed) {
            float ay = std::atan2(lightDir_.z, lightDir_.x);
            float ax = std::asin(lightDir_.y);
            ay += glm::radians(d.x * 0.1f);
            ax  = std::clamp(ax + glm::radians(d.y * 0.1f), -1.5f, 1.5f);
            lightDir_ = glm::normalize(glm::vec3(
                std::cos(ax)*std::cos(ay),
                std::sin(ax),
                std::cos(ax)*std::sin(ay)));
        }
    }

    void scrollCallback(GLFWwindow* w, double xo, double yo) override {
        static double acc = 0.0;
        acc += yo;
        if (std::abs(acc) >= 1.0) {
            setLOD(lod_ + (acc > 0 ? -1 : 1));
            acc = 0.0;
        }
    }

private:
    // -----------------------------------------------------------------------
    void setLOD(int newLod) {
        newLod = std::clamp(newLod, 0, exrLoader_->getNumLODs() - 1);
        if (newLod == lod_) return;
        lod_ = newLod;
        needsReload_ = true;
        std::cout << ">>> LOD " << lod_
                  << "  (" << (512 >> lod_) << " ppd)\n";
    }

    void adjustLat(double d) {
        const double n = std::clamp(lat_ + d, kMinLat, kMaxLat);
        if (std::fabs(n - lat_) > 1e-9) { lat_ = n; needsReload_ = true; logPos(); }
    }
    void adjustLon(double d) {
        const double n = wrapLon(lon_ + d);
        if (std::fabs(n - lon_) > 1e-9) { lon_ = n; needsReload_ = true; logPos(); }
    }
    void logPos() const {
        std::cout << "Center: " << lat_ << " N, " << lon_ << " E  LOD " << lod_ << "\n";
    }

    void centerCamera() {
        camera->target   = {width_/2.f, height_/2.f, 0.f};
        camera->distance = 600.f;
        camera->yaw      = 90.f;
        camera->pitch    = 60.f;
        camera->updateVectors();
    }

    // Build mesh from elevation data
    void buildMesh(const std::vector<float>& elev,
                   const std::vector<std::array<float,3>>& colors) {
        const float scaleZ = 1000.f / 30.325f;

        mesh->vertices.clear();
        mesh->indices.clear();
        mesh->vertices.reserve(static_cast<size_t>(width_) * height_ * 7);
        mesh->indices.reserve(static_cast<size_t>(width_-1) * (height_-1) * 6);

        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const size_t i = static_cast<size_t>(y) * width_ + x;
                const float  e = elev[i];
                const auto&  c = colors[i];
                mesh->vertices.push_back(static_cast<float>((width_-1) - x));
                mesh->vertices.push_back(static_cast<float>(y));
                mesh->vertices.push_back(e * scaleZ);
                mesh->vertices.push_back(e);
                mesh->vertices.push_back(c[0]);
                mesh->vertices.push_back(c[1]);
                mesh->vertices.push_back(c[2]);
            }
        }
        for (int y = 0; y < height_-1; ++y)
            for (int x = 0; x < width_-1; ++x) {
                unsigned tl = static_cast<unsigned>(y*width_+x);
                unsigned tr = tl+1, bl = tl+width_, br = bl+1;
                mesh->indices.insert(mesh->indices.end(), {tl,bl,tr, tr,bl,br});
            }
    }

    void updateCurvature() {
        const float ppd = static_cast<float>(512 >> lod_);
        const float dpp = 1.f / std::max(ppd, 1.f);
        const float lonSpan = width_  * dpp;
        const float latSpan = height_ * dpp;
        const float hw = width_  * 0.5f;
        const float hh = height_ * 0.5f;
        curvature_ = std::max(
            hw  > 0.f ? glm::radians(lonSpan*0.5f)/hw  : 0.f,
            hh  > 0.f ? glm::radians(latSpan*0.5f)/hh  : 0.f);
        nwCorner_ = {lat_ + latSpan*0.5f, lon_ - lonSpan*0.5f};
        seCorner_ = {lat_ - latSpan*0.5f, lon_ + lonSpan*0.5f};
        lonSpanDeg_ = lonSpan;
        latSpanDeg_ = latSpan;
    }

    void loadTerrain() {
        auto elev = exrLoader_->loadOrUpdateTerrain(lat_, lon_, width_, height_, lod_);
        if (elev.empty()) throw std::runtime_error("Failed to load terrain from OpenEXR pyramid");

        minElev_ = *std::min_element(elev.begin(), elev.end());
        maxElev_ = *std::max_element(elev.begin(), elev.end());
        updateCurvature();
        auto colors = colorSampler_->sampleColorsForTerrain(lat_, lon_, width_, height_, latSpanDeg_, lonSpanDeg_);
        buildMesh(elev, colors);
        std::cout << "Mesh ready: " << width_ << "x" << height_
                  << "  LOD " << lod_
                  << "  elev [" << minElev_ << ", " << maxElev_ << "] m\n";
    }

    void reloadTerrain() {
        needsReload_ = false;
        auto elev = exrLoader_->loadOrUpdateTerrain(lat_, lon_, width_, height_, lod_);
        if (elev.empty()) return;

        minElev_ = *std::min_element(elev.begin(), elev.end());
        maxElev_ = *std::max_element(elev.begin(), elev.end());
        updateCurvature();
        auto colors = colorSampler_->sampleColorsForTerrain(lat_, lon_, width_, height_, latSpanDeg_, lonSpanDeg_);

        const float scaleZ = 1000.f / 30.325f;
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const size_t di = static_cast<size_t>(y)*width_ + x;
                const size_t vi = di * 7;
                if (vi + 6 >= mesh->vertices.size()) continue;
                const float e = elev[di];
                mesh->vertices[vi+2] = e * scaleZ;
                mesh->vertices[vi+3] = e;
                mesh->vertices[vi+4] = colors[di][0];
                mesh->vertices[vi+5] = colors[di][1];
                mesh->vertices[vi+6] = colors[di][2];
            }
        }
        mesh->updateVertexData();
    }

    void printHelp() const {
        std::cout <<
            "\n===================================================\n"
            "  LUNAR SURFACE VIEWER — OpenEXR Pyramid Edition\n"
            "===================================================\n"
            "  Mouse Left   : Orbit camera\n"
            "  Mouse Right  : Rotate sunlight\n"
            "  Scroll / [/] : Change LOD level\n"
            "  Keys 0 – 6   : Select LOD directly\n"
            "  WASD/Arrows  : Pan (lat/lon)\n"
            "  C            : Toggle colour mode (Relief/Colormap)\n"
            "  R            : Reset camera\n"
            "===================================================\n\n";
    }

    // Member state
    std::unique_ptr<ColorMapSampler> colorSampler_;
    std::unique_ptr<TerrainLoaderEXR> exrLoader_;
    std::unique_ptr<ShaderProgram>   shader_;
    std::string                      dataRoot_;

    int    width_  = 512;
    int    height_ = 512;
    int    lod_    = 4;   // start at LOD 4 (1440×960) for instant first frame
    double lat_    = kDefaultLat;
    double lon_    = wrapLon(kDefaultLon);
    float  minElev_ = 0.f, maxElev_ = 0.f;
    float  colorMode_ = 0.f;
    float  curvature_ = 0.f;
    float  lonSpanDeg_ = 0.f, latSpanDeg_ = 0.f;
    glm::vec2 nwCorner_{}, seCorner_{};
    glm::vec3 lightDir_{};
    glm::vec2 screenSize_{};
    FontOverlay fpsOverlay_;
    bool needsReload_ = false;

    GLint uModel_=-1, uView_=-1, uProj_=-1;
    GLint uMinElev_=-1, uMaxElev_=-1, uLight_=-1, uColorMode_=-1;
    GLint uCurv_=-1, uCenter_=-1, uNW_=-1, uSE_=-1, uDims_=-1;
};

// ===========================================================================
int main(int argc, char** argv) {
    std::string root = (argc > 1) ? argv[1] : "./";
    try {
        LunarViewerEXR app("Lunar Viewer — OpenEXR Pyramid", std::move(root));
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
