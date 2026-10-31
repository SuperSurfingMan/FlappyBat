#include "sprite/Sprite.h"

class ColorfulSprite : public Sprite {
public:
    ColorfulSprite() = default;
    ~ColorfulSprite() = default;

    explicit ColorfulSprite(std::shared_ptr<Texture2D> baseTexture, float periodTime);

    void Update(float dt) override;
    void Reset() override;
    
    float GetProgress() const;
private:
    const float period;
    float timer = 0.f;
};