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
    
    void InitAnimations(std::string type, size_t normal_tex_row, size_t normal_tex_col);

    void Update(float dt) override;
private:
    std::function<void()> callback;
    bool isCollected = false;

    std::unique_ptr<Animation> normalAnimation;
    std::unique_ptr<Animation> triggerAnimation;
};