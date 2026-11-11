#include "sprite/BatSprite.h"
#include "core/Input.h"
#include "core/Config.h"
#include "audio/AudioSystem.h"
#include "res/ResourceManager.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>
#include <cmath>

BatSprite::BatSprite(std::shared_ptr<Texture2D> texture, ResourceManager& res, const Config& cfg) : Sprite(texture)
{
    // 初始化飞行动画
    std::cout << "init bat fly sheet \n";
    auto flySpriteSheet = res.GetTexture2D("batFlySheet");
    flyAnimation = std::make_unique<Animation>(true, false); 
    flyAnimation->InitAnimationUV(flySpriteSheet, 4);

    std::cout << "init bat dead sheet \n";
    auto deadSpriteSheet = res.GetTexture2D("batDeadSheet");
    deadAnimation = std::make_unique<Animation>(false, true);
    deadAnimation->InitAnimationUV(deadSpriteSheet, 7);


    animation = std::move(flyAnimation); // 设置当前动画为飞行动画

    std::cout << "load config \n";
    LoadParamsFromConfig(cfg); // 从配置加载参数
}

void BatSprite::Update(float dt) {
    if (invincibleTimer.IsRunning()) invincibleTimer.Update(dt);
    Sprite::Update(dt);
    if (isInvincible)
    {
        float glow = 0.5f + 0.5f * sin(invincibleTimer.GetElapsed() * 8.0f);
        SetGlow(glow * 1.5f);   // 强度倍率
    }
    else
    {
        SetGlow(0.0f);
    }

    // 横移倾角（渲染）：向右移顺时针（负角）、向左移逆时针（正角），平滑过渡
    float bankTarget = -MaxBankAngle * std::clamp(horizontalSpeed / HorizontalMoveSpeed, -1.0f, 1.0f);
    bankAngle += (bankTarget - bankAngle) * std::min(1.0f, dt * 10.0f);
    SetRotation(Vec2(std::cos(bankAngle), std::sin(bankAngle)));
}

bool BatSprite::GetHitState() const {
    return batHit;
}

void BatSprite::SetHitState(bool hitState) {
    batHit = hitState;
}

void BatSprite::OnBatHit() {
    SwitchToAnimation(1);
    state = BatState::Dead;
    velocity = 0.0f;
    horizontalSpeed = 0.0f; // 死亡后停止横移，倾角在 Update 中自然回正
    batHit = true;
}

void BatSprite::ResetState() {
    batHit = false;
    SwitchToAnimation(0);
    state = BatState::Flying;
    velocity = 0.f;
    horizontalSpeed = 0.0f;
    bankAngle = 0.0f;
    SetRotation(Vec2(1.0f, 0.0f));
    this->isInvincible = false;
    this->invincibleTimer.Reset();
}

void BatSprite::SetInvincible(float invincibleTime) {
    isInvincible = true;
    invincibleTimer.Start(invincibleTime, [this](){
        this->isInvincible = false;
        this->invincibleTimer.Reset();
    });
}

bool BatSprite::GetInvincibleState() const {
    return isInvincible;
}

float BatSprite::GetInvincibleRemaining() const {
    return invincibleTimer.IsRunning() ? invincibleTimer.GetRemaining() : 0.0f;
}

float BatSprite::GetVelocity() const {
    return velocity;
}

void BatSprite::SetVelocity(float v) {
    velocity = v;
}

float BatSprite::GetHorizontalSpeed() const {
    return horizontalSpeed;
}

void BatSprite::SetHorizontalSpeed(float v) {
    horizontalSpeed = v;
}

void BatSprite::JumpNow() {
    velocity = jumpStrength;
}

void BatSprite::Falling(float dt) {
    velocity -= gravity * dt;
    auto pos = GetPosition();
    pos.y += velocity * dt; // 注意：y轴向上为正
    pos.x += horizontalSpeed * dt;
    pos.x = std::clamp(pos.x, xBoundMin, xBoundMax); // 横向限制在窗口内
    // 纵向钳制在地面/天花板致死线内：常规局触线当帧判死（判定阈值与本边界一致，行为不变）；
    // 无敌（护盾/开局保护/巨化/俯冲无敌帧）与怪物段落期免死，若不钳制会径直沉出地图外——改为贴线滑行
    if (pos.y < yBoundMin) {
        pos.y = yBoundMin;
        velocity = 0.0f; // 贴线不累积下坠速度，免死结束后由边界判定接管
    } else if (pos.y > yBoundMax) {
        pos.y = yBoundMax;
        velocity = 0.0f;
    }
    SetPosition(pos.x, pos.y);
}

void BatSprite::ConstrainWithinWindow() {
    auto pos = GetPosition();
    pos.x = std::clamp(pos.x, xBoundMin, xBoundMax);
    pos.y = std::clamp(pos.y, yBoundMin, yBoundMax);
    SetPosition(pos.x, pos.y);
}

void BatSprite::SwitchToAnimation(int animIndex) {
    if (state == BatState::Dead) {
        deadAnimation = std::move(animation);
        deadAnimation->Reset();
    }
    else if (state == BatState::Flying) {
        flyAnimation = std::move(animation); // 保存当前飞行动画状态
        flyAnimation->Reset();
    }

    if (animIndex == 0) {
        // 切换到飞行动画
        animation = std::move(flyAnimation);
    } else if (animIndex == 1) {
        // 切换到死亡动画
        animation = std::move(deadAnimation); // 切换当前动画到死亡动画
    }
}

void BatSprite::LoadParamsFromConfig(const Config& cfg) {
    gravity = cfg.GetGravity();
    jumpStrength = cfg.GetFlapStrength();
    // 横向活动边界：窗口内侧留出半身位余量（蝙蝠 64px）
    float screenWidth = (float)cfg.GetScreenWidth();
    xBoundMin = 40.0f;
    xBoundMax = screenWidth - 40.0f;
    // 纵向活动边界：与 MainScene::CollisionDetect 的撞地/撞顶致死线重合
    // （地面线 terrain_height + 触边半高，天花板线 屏高 - 触边半高）
    yBoundMin = cfg.GetTerrainHeight() + EdgeContactMargin;
    yBoundMax = (float)cfg.GetScreenHeight() - EdgeContactMargin;
}

BatSprite::~BatSprite() {
    
}