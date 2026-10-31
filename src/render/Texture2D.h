#pragma once

#include <string>
#include <vector>
#include <optional>
#include <glad/glad.h>
#include "core/Math.h"

// 解码后的裸像素数据，不含任何 GL 资源，可跨线程传递
struct DecodedImage
{
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
};

struct FrameUV
{
    Vec2 uvMin;
    Vec2 uvMax;

    static FrameUV Zero() {
        return {{0.f, 0.f}, {0.f, 0.f}};
    }
};

class Texture2D {
public:
    Texture2D();
    ~Texture2D();

    // 禁止拷贝（OpenGL 资源不应被拷贝）
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    // 允许移动
    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    bool LoadFromFile(const std::string& path, bool flipY = true);
    bool LoadFromMemory(
        const unsigned char* data,
        int width,
        int height,
        int channels
    );

    // 仅磁盘 IO + 解码，不触碰 GL，可在后台线程调用
    static std::optional<DecodedImage> DecodeFromFile(const std::string& path, bool flipY = true);
    // 将已解码像素上传到 GPU（mipmap/采样参数与 LoadFromFile 完全一致），必须在持有 GL 上下文的线程调用
    bool UploadDecoded(const DecodedImage& image);

    static std::vector<FrameUV> ClipFramesFromSpriteSheet(size_t frameCount);
    static std::vector<FrameUV> ClipFramesFromSpriteSheet(size_t rows, size_t columns);

    void Bind(GLuint unit = 0) const;
    void Unbind() const;

    int GetWidth()  const { return width; }
    int GetHeight() const { return height; }
    GLuint GetID()  const { return id; }

private:
    GLuint id = 0;
    int width = 0;
    int height = 0;
    int channels = 4;
};
