#pragma once
#include "sprite/Sprite.h"
#include "core/Timer.h"

enum class BatState {
    Flying,
    Dead
};

class Config;
class ResourceManager;

class BatSprite : public Sprite {
public:
    BatSprite() = default;
    ~BatSprite();
    // 配置在构造时注入：重力/振翅/活动边界全部来自 Config，
    // 精灵自身不再持有全局访问点（可由自测直接构造并断言边界）。
    BatSprite(std::shared_ptr<Texture2D> texture, ResourceManager& res, const Config& cfg);

    void Update(float dt) override;

    void SwitchToAnimation(int animIndex) override;

    void LoadParamsFromConfig(const Config& cfg);

    bool GetHitState() const;

    void SetHitState(bool hitState);

    void OnBatHit();

    void ResetState();

    void SetInvincible(float invincibleTime);

    bool GetInvincibleState() const;
    float GetInvincibleRemaining() const;  // 剩余无敌秒数（未无敌为 0；授予方防缩短用）

    float GetVelocity() const;
    void SetVelocity(float v);

    // 物理参数只读（AutoPlay 感知填充用：预测轨迹须与实际积分同源）
    float GetGravity() const { return gravity; }
    float GetJumpStrength() const { return jumpStrength; }

    // f: 垂直速度 -> 相对原贴图的俯仰目标角（弧度，纯函数，渲染用）。
    // 模仿 Flappy Bird 的姿态机制：振翅瞬间满上仰（25°），无操作失速后
    // 渐进低头、长坠饱和于垂直俯冲（90°）。两段均取 smoothstep（两端导数为 0）：
    // 顶点处无折角、终端速度附近不抖动、中段翻头果断——不是线性映射。
    // 角度约定同 bankAngle：正=逆时针（抬头）、负=顺时针（低头）。
    static float PitchAngleFromVelocity(float velocity, float flapSpeed);

    // ===== 横向移动（躲避全高雷柱用；A/D 或 ←/→ 持续按住） =====
    static constexpr float HorizontalMoveSpeed = 200.0f; // 满速横移速度（px/s）
    float GetHorizontalSpeed() const;
    void SetHorizontalSpeed(float v); // 正=向右，负=向左

    void JumpNow();
    // driftY：阵风等外力折算的纵向漂移速度(px/s)，直接叠加进位置积分。
    // 不写回 velocity——振翅会整个覆写 velocity，写回去会让风在每次拍翅时失效。
    void Falling(float dt, float driftY = 0.0f);

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
    float pitchAngle = 0.0f;      // 当前俯仰姿态角（渲染用，弧度；正=抬头/逆时针）
    float xBoundMin = 40.0f;      // 横向活动左边界（半身位余量）
    float xBoundMax = 500.0f;     // 横向活动右边界（LoadParamsFromConfig 按窗口宽计算）
    // 纵向活动边界（LoadParamsFromConfig 计算）：下界=地面致死线（terrain_height+EdgeContactMargin）、
    // 上界=天花板致死线（屏高-EdgeContactMargin）。两界必须与触边致死判定同线：
    // 常规局触线当帧判死不被钳制吞掉；无敌/段落期免死时则贴线滑行，不再越出地图
    float yBoundMin = 109.0f;     // 默认配置（terrain 99 + 触边半高 10）下的地面线
    float yBoundMax = 950.0f;     // 默认配置（屏高 960 - 触边半高 10）下的天花板线
    static constexpr float MaxBankAngle = 0.30f; // 满速横移时的最大倾角（约 17°）

    // ===== 俯仰姿态（模仿 Flappy Bird：振翅抬头 / 无操作失速低头） =====
    // 目标角由 PitchAngleFromVelocity 给出，Update 中指数平滑后与 bankAngle 合成。
    // 相对原贴图转角值域 [-90°, +25°]：上仰上限 25°（振翅瞬间满上仰）、
    // 俯冲上限 90°（长坠垂直扎下）——均为 Flappy Bird 经典取值；
    // 若按“正=顺时针（低头为正）”的表述则值域为 [-25°, +90°]。
    static constexpr float MaxPitchUpRad   = 0.43633f; // 上仰上限（25°）
    static constexpr float MaxPitchDownRad = 1.57080f; // 俯冲上限（90°，即 π/2）
    static constexpr float PitchEaseRate   = 15.0f;    // 俯仰角向目标收敛的速率（1/秒，指数平滑）
    static constexpr float DownEngageV     = 0.40f;    // 下坠低头启动速度（×振翅初速）：越过后开始低头
    static constexpr float DownFullV       = 3.20f;    // 下坠满俯冲速度（×振翅初速）：越过后饱和于 90°

    bool batHit = false;

    bool isInvincible = false;
    Timer invincibleTimer;

    // float invincibleVisualTimer = 0.0f;
    // float flickerInterval = 0.1f; // 10Hz 闪烁
    // bool visible = true;
};