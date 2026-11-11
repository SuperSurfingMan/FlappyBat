#include "sprite/CollectableSprite.h"
#include "render/Animation.h"
#include "res/ResourceManager.h"
#include <iostream>

void CollectableSprite::SetCallback(std::function<void()>&& cb) {
    callback = cb;
}

void CollectableSprite::OnCollide() {
    if (!isCollected) {
        // 拾取音效与收益结算都由持有方（ItemField -> 场景）决定：
        // 精灵只负责"被摸到了"这件事与自身的消散动画。
        if (callback) callback();
        // 当前问题: 摸到item之后动画直接没了 已解决
        SwitchToAnimation(1);
        std::cout << animation.get() << std::endl;
        isCollected = true;
        std::cout << "switch to trigger animation" << std::endl;
    }
}

bool CollectableSprite::GetCollectState() {
    return isCollected;
}

void CollectableSprite::Reset() {
    Sprite::Reset();
    SwitchToAnimation(0);
    isCollected = false;

    std::cout << animation.get() << std::endl;
    std::cout << triggerAnimation.get() << std::endl;
}

bool CollectableSprite::CheckCollision(const Vec2& position, const Vec2& scale) {
    float halfW1 = this->scale.x / 2.0f;
    float halfH1 = this->scale.y / 2.0f;
    float halfW2 = scale.x / 2.0f;
    float halfH2 = scale.y / 2.0f;
    
    if (std::abs(this->position.x - position.x) < halfW1 + halfW2 &&
        std::abs(this->position.y - position.y) < halfH1 + halfH2) {
        OnCollide();
        return true;
    }
    return false;
}

void CollectableSprite::InitAnimations(std::string type, size_t normal_tex_row,
                                       size_t normal_tex_col, int disappearFrames,
                                       ResourceManager& res) {
    std::shared_ptr<Texture2D> normalSpritesheet = res.GetTexture2D(type);
    std::string disappearTextureKey = type + "Disappear";
    std::shared_ptr<Texture2D> disappearSpritesheet = res.GetTexture2D(disappearTextureKey);
    if (!normalSpritesheet || !disappearSpritesheet) {
        // 贴图键是运行时拼串约定（<type> / <type>Disappear）。缺一个原先会静默 return，
        // 表现为「道具永远不显示且无报错」；现在显式指出是哪个键没登记。
        std::cerr << "[CollectableSprite] 贴图键未登记: \"" << type
                  << "\" 或 \"" << disappearTextureKey
                  << "\"，该道具无法显示（请检查 ResourceManager 资源清单）" << std::endl;
        return;
    }

    normalAnimation = std::make_unique<Animation>(true, false);
    normalAnimation->InitAnimationUV(normalSpritesheet, normal_tex_row, normal_tex_col);

    triggerAnimation = std::make_unique<Animation>(false, true);
    triggerAnimation->InitAnimationUV(disappearSpritesheet, (size_t)disappearFrames);
    triggerAnimation->setOnAnimationFinished([this](){
        setActive(false);
    });

    animation = std::move(normalAnimation);
    isCollected = false;
}

void CollectableSprite::SwitchToAnimation(int animIndex) {
    if (animIndex < 0 || animIndex > 1) return;

    if (!isCollected) {
        normalAnimation = std::move(animation);
        normalAnimation->Reset();
    }
    else {
        triggerAnimation = std::move(animation);
        triggerAnimation->Reset();
    }

    if (animIndex == 0) {
        animation = std::move(normalAnimation);
    }
    else if (animIndex == 1) {
        animation = std::move(triggerAnimation);
    }
}

void CollectableSprite::Update(float dt) {
    Sprite::Update(dt);
}