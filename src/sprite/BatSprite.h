#pragma once
#include "Sprite.h"
#include "../timer/Timer.h"

enum class BatState {
    Flying,
    Dead
};

class BatSprite : public Sprite {
public:
    BatSprite() = default;
    ~BatSprite();
    explicit BatSprite(std::shared_ptr<Texture2D> texture);

    void Update(float dt) override;

    void SwitchToAnimation(int animIndex) override;

    void LoadParamsFromConfig();

    bool GetHitState() const;

    void SetHitState(bool hitState);

    void OnBatHit();

    void ResetState();

    void SetInvincible(float invincibleTime);

    bool GetInvincibleState() const;
    float GetInvincibleRemaining() const;  // 剩余无敌秒数（未无敌为 0；授予方防缩短用）

    float GetVelocity() const;
    void SetVelocity(float v);

    // ===== 横向移动（躲避全高雷柱用；A/D 或 ←/→ 持续按住） =====
    static constexpr float HorizontalMoveSpeed = 200.0f; // 满速横移速度（px/s）
    float GetHorizontalSpeed() const;
    void SetHorizontalSpeed(float v); // 正=向右，负=向左

    void JumpNow();
    void Falling(float dt);

    // 物理积分之外的直接位移（击退/弹起等）后统一收口：把位置钳回窗口活动范围
    void ConstrainWithinWindow();

    // 触边判定半高：与 MainScene::CollisionDetect 撞地/撞顶（及枭王段落期同阈值受击）共用，
    // 也是纵向活动边界相对地面线/屏顶的内缩量——改此值须同步核对致死判定线
    static constexpr float EdgeContactMargin = 10.0f;

private:
    std::unique_ptr<Animation> flyAnimation;
    std::unique_ptr<Animation> deadAnimation;
    BatState state = BatState::Flying;

    float gravity = 500.0f; // 重力加速度
    float velocity = 0.0f;  // 当前垂直速度
    float jumpStrength = 250.0f; // 跳跃强度

    float horizontalSpeed = 0.0f; // 当前横向速度（Falling 中积分并夹紧在窗口边界内）
    float bankAngle = 0.0f;       // 当前横滚倾角（渲染用，弧度；正=逆时针）
    float xBoundMin = 40.0f;      // 横向活动左边界（半身位余量）
    float xBoundMax = 500.0f;     // 横向活动右边界（LoadParamsFromConfig 按窗口宽计算）
    // 纵向活动边界（LoadParamsFromConfig 计算）：下界=地面致死线（terrain_height+EdgeContactMargin）、
    // 上界=天花板致死线（屏高-EdgeContactMargin）。两界必须与触边致死判定同线：
    // 常规局触线当帧判死不被钳制吞掉；无敌/段落期免死时则贴线滑行，不再越出地图
    float yBoundMin = 109.0f;     // 默认配置（terrain 99 + 触边半高 10）下的地面线
    float yBoundMax = 950.0f;     // 默认配置（屏高 960 - 触边半高 10）下的天花板线
    static constexpr float MaxBankAngle = 0.30f; // 满速横移时的最大倾角（约 17°）

    bool batHit = false;

    bool isInvincible = false;
    Timer invincibleTimer;

    // float invincibleVisualTimer = 0.0f;
    // float flickerInterval = 0.1f; // 10Hz 闪烁
    // bool visible = true;
};