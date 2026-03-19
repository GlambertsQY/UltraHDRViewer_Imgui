#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct GLFWwindow;

struct ImageData {
    uint8_t* pixels = nullptr;   // Always RGBA8888 for display
    int width = 0;
    int height = 0;
    int channels = 4;
    std::string filePath;

    ~ImageData();

    size_t dataSize() const {
        return (size_t)width * height * channels;
    }
};

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool init(GLFWwindow* window) = 0;
    virtual void shutdown() = 0;

    virtual void newFrame() = 0;
    virtual void render() = 0;

    virtual void* createTexture(const ImageData& image) = 0;
    virtual void destroyTexture(void* texture) = 0;
    virtual void updateTexture(void* texture, const ImageData& image) = 0;

    static std::unique_ptr<Renderer> createOpenGL();
    static std::unique_ptr<Renderer> createVulkan();
};
