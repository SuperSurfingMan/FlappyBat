#include "sprite/CollectableSprite.h"
#include "render/Animation.h"
#include "res/ResourceManager.h"
#include "audio/AudioSystem.h"
#include <iostream>

void CollectableSprite::SetCallback(std::function<void()>&& cb) {
    callback = cb;
}

void CollectableSprite::OnCollide() {
    if (!isCollected) {
        if (callback) callback();
        // 收集道具音效
        AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Collect), 0.9f);
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

void CollectableSprite::InitAnimations(std::string type, size_t normal_tex_row, size_t normal_tex_col) {
    std::shared_ptr<Texture2D> normalSpritesheet = ResourceManager::Get().GetTexture2D(type);
    std::string disappearTextureKey = type + "Disappear";
    std::shared_ptr<Texture2D> disappearSpritesheet = ResourceManager::Get().GetTexture2D(disappearTextureKey);
    if (!normalSpritesheet || !disappearSpritesheet) return;

    normalAnimation = std::make_unique<Animation>(true, false);
    normalAnimation->InitAnimationUV(normalSpritesheet, normal_tex_row, normal_tex_col);

    triggerAnimation = std::make_unique<Animation>(false, true);
    if (type == "kiwi") {
        triggerAnimation->InitAnimationUV(disappearSpritesheet, 6);
    }
    else {
        triggerAnimation->InitAnimationUV(disappearSpritesheet, 8);
    }
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