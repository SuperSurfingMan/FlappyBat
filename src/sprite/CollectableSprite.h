#pragma once
#include "sprite/Sprite.h"
#include <functional>
#include <string>

class Animation;

class CollectableSprite : public Sprite {
public:
    CollectableSprite() {}
    explicit CollectableSprite(std::shared_ptr<Texture2D> texture) : Sprite(texture){}

    void SetCallback(std::function<void()>&& cb);

    void OnCollide();

    bool GetCollectState();

    void Reset() override;

    bool CheckCollision(const Vec2& position, const Vec2& scale);

    void SwitchToAnimation(int animIndex) override;
    
    // 动画网格与消散帧数均来自 ItemCatalog 定义表，本类不再按类型名分支
    void InitAnimations(std::string type, size_t normal_tex_row, size_t normal_tex_col,
                        int disappearFrames);

    void Update(float dt) override;
private:
    std::function<void()> callback;
    bool isCollected = false;

    std::unique_ptr<Animation> normalAnimation;
    std::unique_ptr<Animation> triggerAnimation;
};