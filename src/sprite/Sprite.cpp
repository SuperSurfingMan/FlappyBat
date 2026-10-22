#include "Sprite.h"
#include "iostream"

Sprite::Sprite() = default;

Sprite::Sprite(std::shared_ptr<Texture2D> texture)
    : texture(texture) {}

void Sprite::SetTexture(std::shared_ptr<Texture2D> texture) {
    this->texture = texture;
}

void Sprite::SetPosition(float x, float y) {
    position.x = x;
    position.y = y;
}

void Sprite::SetScale(float x, float y) {
    scale.x = x;
    scale.y = y;
}

void Sprite::GetTransform(float out[16]) const
{
    float cosT = rotation.x;
    float sinT = rotation.y;

    float sx = scale.x;
    float sy = scale.y;

    out[0]  =  sx * cosT;
    out[4]  = -sy * sinT;
    out[8]  =  0.0f;
    out[12] =  position.x;

    out[1]  =  sx * sinT;
    out[5]  =  sy * cosT;
    out[9]  =  0.0f;
    out[13] =  position.y;

    out[2]  =  0.0f;
    out[6]  =  0.0f;
    out[10] =  1.0f;
    out[14] =  0.0f;

    out[3]  =  0.0f;
    out[7]  =  0.0f;
    out[11] =  0.0f;
    out[15] =  1.0f;
}

void Sprite::Update(float dt) {
    if (isActive && animation) {
        if (animation->IsFrameListEmpty())
            texture = animation->GetSpriteSheet();
        else
            texture = animation->GetCurrentFrame();
        
        currentFrameUV = animation->GetCurrentFrameUV();
        animation->Update(dt);
    }
}

void Sprite::InitAnimationByTexture(std::shared_ptr<Texture2D> spriteSheet, size_t frameCount) {
    animation = std::make_unique<Animation>();
    animation->InitAnimationUV(spriteSheet, frameCount);
}

void Sprite::InitAnimationByTexture(std::shared_ptr<Texture2D> spriteSheet, size_t rows, size_t columns) {
    animation = std::make_unique<Animation>();
    animation->InitAnimationUV(spriteSheet, rows, columns);
}

void Sprite::InitAnimationByTexture(std::shared_ptr<Texture2D> spriteSheet, std::vector<float> uvSegPoints) {
    animation = std::make_unique<Animation>();
    std::vector<FrameUV> uvCords;
    size_t s = uvSegPoints.size();
    uvCords.push_back(FrameUV{Vec2{0.f, 0.f}, Vec2{uvSegPoints[0], 1.f}});
    for (size_t i = 0; i < s; ++i) {
        float end_u = (i == (s - 1)) ? 1.f : uvSegPoints[i+1];
        uvCords.push_back(FrameUV{Vec2{uvSegPoints[i], 0.f}, Vec2{end_u, 1.f}});
    }
    std::cout << "big bat use uvCords size: " << s << "\n";
    s = uvCords.size();
    for (size_t i = 0;i < s; ++i) {
        std::cout << "start uv:" << uvCords[i].uvMin.x << " "<< uvCords[i].uvMin.y << " end uv: " << uvCords[i].uvMax.x << " " << uvCords[i].uvMax.y << "\n";
    }
    animation->InitAnimationUV(spriteSheet, uvCords);
}

void Sprite::InitEmptyAnimation() {
    animation = std::make_unique<Animation>();
}

void Sprite::SwitchToAnimation(int animIndex) {
    // 这里可以根据 animIndex 切换到不同的动画
    // 目前示例中只有一个动画，所以暂时不实现具体逻辑
}

void Sprite::setAnimationCallback(std::function<void()> callback) {
    if (animation) {
        animation->setOnAnimationFinished(callback);
    }
}

const std::shared_ptr<Texture2D>& Sprite::GetTexture() const {
    return texture;
}

const std::unique_ptr<Animation>& Sprite::GetAnimation() const {
    return animation;
}

void Sprite::Reset() {
    if (animation) animation->Reset();
    rotation = {1, 0};
    SetColor(1.f, 1.f, 1.f, 1.f);
    glow = 0.f;
    isActive = true;
}

void Sprite::SetAlpha(float a)
{
    color[3] = a;
}

void Sprite::SetColor(float r, float g, float b, float a)
{
    color[0] = r;
    color[1] = g;
    color[2] = b;
    color[3] = a;
}

void Sprite::SetRotation(Vec2 r)
{
    rotation = r;
}

const float* Sprite::GetColor() const
{
    return color;
}

FrameUV Sprite::GetFrameUV() const {
    return currentFrameUV;
}

void Sprite::SetGlow(float g)
{
    glow = g;
}

float Sprite::GetGlow() const
{
    return glow;
}

void Sprite::SetLumaKey(bool enabled, float thresh, float soft) {
    useLumaKey = enabled;
    lumaThresh = thresh;
    lumaSoft = soft;
}

Sprite::~Sprite() {
    if (texture) {
        texture = nullptr; // 不拥有 Texture2D，不负责销毁
    }
}