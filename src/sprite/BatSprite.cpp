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

// f: 垂直速度 -> 俯仰目标角（弧度）。曲线设计参考 Flappy Bird 的姿态机制：
// 振翅瞬间抬头 25°、无操作失速后渐进低头、长坠饱和于 90° 垂直俯冲。
// 上仰段按 vy/vFlap 归一；下坠段以 [0.4·vFlap, 3.2·vFlap] 为区间做 smoothstep——
// 非线性 S 形让刚过顶点的滑行保持平飞、中段果断翻头、终端速度附近不再抖动。
float BatSprite::PitchAngleFromVelocity(float velocity, float flapSpeed) {
    // smoothstep：两端导数为 0 的 S 形缓动
    auto smoothstep = [](float t) { return t * t * (3.0f - 2.0f * t); };

    if (velocity > 0.0f) {
        // 上升段：振翅瞬间 vy=vFlap → 立即满上仰；升至顶点渐回平
        const float t = std::clamp(velocity / flapSpeed, 0.0f, 1.0f);
        return MaxPitchUpRad * smoothstep(t);
    }
    // 下坠段：越过低头启动速度前保持平飞（滑行），随后渐进翻头、长坠垂直扎下
    const float engage = DownEngageV * flapSpeed;
    const float span   = (DownFullV - DownEngageV) * flapSpeed;
    const float t = std::clamp((-velocity - engage) / span, 0.0f, 1.0f);
    return -MaxPitchDownRad * smoothstep(t);
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

    // 俯仰姿态（渲染）：目标角来自 f(vy)，指数平滑后合成。
    // 振翅瞬间目标角突跳约 36°，平滑把它摊到 ~0.15s——保留“抬头一瞬”的利落
    // 又避免单帧跳变；下坠段目标角随速度连续变化，平滑只带来微小滞后。
    const float pitchTarget = PitchAngleFromVelocity(velocity, jumpStrength);
    pitchAngle += (pitchTarget - pitchAngle) * std::min(1.0f, dt * PitchEaseRate);

    // 横移倾角（渲染）：向右移顺时针（负角）、向左移逆时针（正角），平滑过渡
    float bankTarget = -MaxBankAngle * std::clamp(horizontalSpeed / HorizontalMoveSpeed, -1.0f, 1.0f);
    bankAngle += (bankTarget - bankAngle) * std::min(1.0f, dt * 10.0f);
    // 俯仰 + 横滚合成单一旋转（2D 旋转可交换，角度直接相加即精确）
    const float attitudeAngle = pitchAngle + bankAngle;
    SetRotation(Vec2(std::cos(attitudeAngle), std::sin(attitudeAngle)));
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
    pitchAngle = 0.0f;
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

void BatSprite::Falling(float dt, float driftY) {
    velocity -= gravity * dt;
    auto pos = GetPosition();
    pos.y += (velocity + driftY) * dt; // 注意：y轴向上为正；漂移项独立于振翅速度
    pos.x += horizontalSpeed * dt;
    pos.x = std::clamp(pos.x, xBoundMin, xBoundMax); // 横向限制在窗口内
    // 纵向钳制在地面/天花板致死线内：常规局触线当帧判死（判定阈值与本边界一致，行为不变）；
    // 无敌护盾/短护盾/开局保护/巨化/受击无敌与怪物段落期免死时，若不钳制会径直沉出地图外——改为贴线滑行
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