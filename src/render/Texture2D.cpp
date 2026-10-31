#include "render/Texture2D.h"

#include <stb_image.h>
#include "core/Util.h"
#include <iostream>

Texture2D::Texture2D() {
    glGenTextures(1, &id);
}

Texture2D::~Texture2D() {
    if (id != 0) {
        glDeleteTextures(1, &id);
    }
}

Texture2D::Texture2D(Texture2D&& other) noexcept {
    id = other.id;
    width = other.width;
    height = other.height;
    channels = other.channels;
    other.id = 0;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        glDeleteTextures(1, &id);
        id = other.id;
        width = other.width;
        height = other.height;
        channels = other.channels;
        other.id = 0;
    }
    return *this;
}

std::optional<DecodedImage> Texture2D::DecodeFromFile(const std::string& path, bool flipY) {
    if (!FileExists(path)) {
        std::cerr << "[Texture2D] File not found: " << path << std::endl;
        return std::nullopt;
    }
    // stbi 的 flip 开关是全局的：当前所有解码路径（主线程预载、后台加载线程）均 flipY=true，无并发冲突
    stbi_set_flip_vertically_on_load(flipY);

    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 0);

    if (!data) {
        std::cerr << "[Texture2D] Failed to load image: " << path << std::endl;
        return std::nullopt;
    }

    DecodedImage image;
    image.width = w;
    image.height = h;
    image.channels = ch;
    image.pixels.assign(data, data + (size_t)w * h * ch);

    stbi_image_free(data);
    return image;
}

bool Texture2D::UploadDecoded(const DecodedImage& image) {
    width = image.width;
    height = image.height;
    channels = image.channels;

    GLenum format = GL_RGB;
    if (channels == 1) format = GL_RED;
    else if (channels == 3) format = GL_RGB;
    else if (channels == 4) format = GL_RGBA;

    glBindTexture(GL_TEXTURE_2D, id);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        format,
        width,
        height,
        0,
        format,
        GL_UNSIGNED_BYTE,
        image.pixels.data()
    );

    glGenerateMipmap(GL_TEXTURE_2D);

    // 基础参数（2D 游戏标准）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

bool Texture2D::LoadFromFile(const std::string& path, bool flipY) {
    auto decoded = DecodeFromFile(path, flipY);
    if (!decoded) {
        system("pause"); // 暂停以便查看错误信息
        exit(EXIT_FAILURE);
        return false;
    }
    return UploadDecoded(*decoded);
}

bool Texture2D::LoadFromMemory(
    const unsigned char* data,
    int w,
    int h,
    int ch
) {
    width = w;
    height = h;
    channels = ch;

    if (id == 0) {
        glGenTextures(1, &id);
    }

    glBindTexture(GL_TEXTURE_2D, id);

    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        format,
        width,
        height,
        0,
        format,
        GL_UNSIGNED_BYTE,
        data
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}


void Texture2D::Bind(GLuint unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, id);
}

void Texture2D::Unbind() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}

std::vector<FrameUV> Texture2D::ClipFramesFromSpriteSheet(size_t frameCount)
{
    std::vector<FrameUV> frames;
    frames.reserve(frameCount);

    float step = 1.0f / frameCount;

    for (int i = 0; i < frameCount; ++i)
    {
        FrameUV uv;
        uv.uvMin = { i * step, 0.0f };
        uv.uvMax = { (i + 1) * step, 1.0f };

        frames.push_back(uv);
    }

    return frames;
}

std::vector<FrameUV> Texture2D::ClipFramesFromSpriteSheet(size_t rows, size_t columns) {
    std::vector<FrameUV> frames;
    if (rows == 0 || columns == 0) {
        return frames;
    }

    size_t frameCount = rows * columns;
    frames.reserve(frameCount);

    float rowStep = 1.0f / static_cast<float>(rows);
    float colStep = 1.0f / static_cast<float>(columns);

    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < columns; ++c) {
            FrameUV uv;
            uv.uvMin = {c * colStep, r * rowStep};
            uv.uvMax = {(c + 1) * colStep, (r + 1) * rowStep};
            frames.push_back(uv);
        }
    }

    return frames;
}
