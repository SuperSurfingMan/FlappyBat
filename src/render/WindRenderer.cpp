#include "render/WindRenderer.h"

#include "render/Shader.h"
#include "render/ShaderLibrary.h"
#include "render/RenderState.h"

#include <glad/glad.h>
#include <cmath>
#include <algorithm>
#include <random>

namespace {
// 细线数量(用户需求:比雨线密集——雨 ~200 滴短划分散四层,风线 48 条集中于
// 玩法平面单层,且更长更连续,视觉密度更高)
constexpr int kLineCount = 48;
// 归一化用最大推力(风暴之夜 220*1.3=286)
constexpr float kMaxForce = 286.0f;
// 流动节奏:全局正弦脉动(时快时慢,如阵风一阵阵推进)
constexpr float kRhythmFreq = 1.6f;  // 节奏频率(Hz 级,弧度/秒)

// 颜色(蓝色系主题,四向微调色温可辨方向):
// 上=天蓝 下=深蓝 左=偏紫蓝 右=偏青蓝
constexpr float kUpColor[3]    = { 0.49f, 0.71f, 1.00f };
constexpr float kDownColor[3]  = { 0.34f, 0.51f, 0.92f };
constexpr float kLeftColor[3]  = { 0.55f, 0.60f, 1.00f };
constexpr float kRightColor[3] = { 0.42f, 0.76f, 0.95f };
}

void WindRenderer::Init(float screenW, float screenH)
{
    screenW_ = screenW;
    screenH_ = screenH;
    shader_ = ShaderLibrary::Instance().Get("wind");

    InitInstancedQuad((size_t)kLineCount, sizeof(WindInstance));

    // iPos -> iDir -> iSize -> iPhase -> iAlpha(location 1~5)
    size_t offset = 0;
    DefineInstanceAttrib({ 1, 2, offset }); offset += sizeof(Vec2);
    DefineInstanceAttrib({ 2, 2, offset }); offset += sizeof(Vec2);
    DefineInstanceAttrib({ 3, 2, offset }); offset += sizeof(Vec2);
    DefineInstanceAttrib({ 4, 1, offset }); offset += sizeof(float);
    DefineInstanceAttrib({ 5, 1, offset });

    // 一次性成批铺满全屏(首阵风出现时即有均匀覆盖)
    lines_.resize(kLineCount);
    for (auto& line : lines_) {
        ReseedLine(line, true);
    }
}

void WindRenderer::ReseedLine(WindLine& line, bool anywhere) {
    static thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> rnd(0.0f, 1.0f);

    const float sw = screenW_;
    const float sh = screenH_;

    line.length = 110.0f + rnd(rng) * 90.0f;                        // 110~200(连续长线)
    line.width  = 1.3f + rnd(rng) * 0.6f;                           // 1.3~1.9(原 0.7~1.1 属亚像素级,无 MSAA 下被光栅化稀释到近乎不可见)
    line.phase  = rnd(rng) * 6.2831853f;
    line.speed  = 0.7f + rnd(rng) * 0.3f;                           // 个体流速系数
    line.alpha  = 0.75f + rnd(rng) * 0.25f;                         // 个体明暗错落(下限抬高保证可见)

    // anywhere=首次铺场/换风向重排:全屏随机;否则从入场端重生(垂直流向轴仍全屏随机)
    if (anywhere) {
        line.pos = Vec2{ 10.0f + rnd(rng) * (sw - 20.0f), rnd(rng) * sh };
    } else {
        if (flowDir_.y > 0.0f)      line.pos = Vec2{ 10.0f + rnd(rng) * (sw - 20.0f), -line.length * 0.5f - rnd(rng) * sh * 0.15f };
        else if (flowDir_.y < 0.0f) line.pos = Vec2{ 10.0f + rnd(rng) * (sw - 20.0f), sh + line.length * 0.5f + rnd(rng) * sh * 0.15f };
        else if (flowDir_.x > 0.0f) line.pos = Vec2{ -line.length * 0.5f - rnd(rng) * sw * 0.15f, 10.0f + rnd(rng) * (sh - 20.0f) };
        else                        line.pos = Vec2{ sw + line.length * 0.5f + rnd(rng) * sw * 0.15f, 10.0f + rnd(rng) * (sh - 20.0f) };
    }
}

void WindRenderer::ResetFx()
{
    gustActive_ = false;
    gustForce_ = Vec2{0.0f, 0.0f};
    intensity_ = 0.0f;
    strength_ = 0.0f;
    time_ = 0.0f;
    flowDir_ = Vec2{0.0f, 1.0f};
    instances_.clear();
    // 线群整批重铺：下一阵风从均匀铺场开始，不复用上一局的位置/朝向
    for (auto& line : lines_) ReseedLine(line, true);
}

void WindRenderer::Update(float dt)
{
    const bool active = gustActive_;
    const Vec2& force = gustForce_;

    time_ += dt;

    bool dirChanged = false;
    if (active && (force.x != 0.0f || force.y != 0.0f)) {
        // 轴对齐流向:非零轴取符号(上(0,1)/下(0,-1)/左(-1,0)/右(1,0))
        Vec2 newDir = Vec2{ force.x != 0.0f ? (force.x > 0.0f ? 1.0f : -1.0f) : 0.0f,
                            force.y != 0.0f ? (force.y > 0.0f ? 1.0f : -1.0f) : 0.0f };
        if (newDir.x != flowDir_.x || newDir.y != flowDir_.y) {
            flowDir_ = newDir;
            dirChanged = true; // 换向:整批重新铺场,避免旧朝向的线横着漂
        }
        strength_ = std::clamp(std::sqrt(force.x * force.x + force.y * force.y) / kMaxForce, 0.0f, 1.0f);
    }

    // 整体起止淡入淡出(阵风开始 ~0.25s 淡入,结束 ~0.35s 淡出)
    const float target = active ? 1.0f : 0.0f;
    const float rate = active ? 4.0f : 2.8f;
    intensity_ += (target - intensity_) * std::min(1.0f, dt * rate);
    if (!active && intensity_ < 0.01f) {
        intensity_ = 0.0f;
        return; // 完全隐没:不再推进粒子
    }

    if (dirChanged) {
        for (auto& line : lines_) ReseedLine(line, true);
        return;
    }

    // ===== 流动节奏:全局正弦脉动(0.55~1.0),全线同步时快时慢如阵风推进 =====
    const float rhythm = 0.775f + 0.225f * std::sin(time_ * kRhythmFreq);

    // 细线沿流向移动;出界后从入场端重生
    const float sw = screenW_;
    const float sh = screenH_;
    for (auto& line : lines_) {
        const float v = (140.0f + 90.0f * line.speed) * strength_ * rhythm * dt;
        line.pos.x += flowDir_.x * v;
        line.pos.y += flowDir_.y * v;

        bool out = false;
        if (flowDir_.y > 0.0f && line.pos.y - line.length * 0.5f > sh) out = true;
        else if (flowDir_.y < 0.0f && line.pos.y + line.length * 0.5f < 0.0f) out = true;
        else if (flowDir_.x > 0.0f && line.pos.x - line.length * 0.5f > sw) out = true;
        else if (flowDir_.x < 0.0f && line.pos.x + line.length * 0.5f < 0.0f) out = true;
        if (out) ReseedLine(line, false);
    }
}

void WindRenderer::Render(const RenderContext& ctx, WorldSlot slot)
{
    if (slot != WorldSlot::WindLines) return;
    if (intensity_ <= 0.01f || !shader_) return;

    // 逻辑数据 -> 实例数据
    instances_.clear();
    instances_.reserve(lines_.size());
    for (const auto& line : lines_) {
        WindInstance& inst = instances_.emplace_back();
        inst.pos = line.pos;
        inst.dir = flowDir_;
        inst.size = Vec2(line.width, line.length);
        inst.phase = line.phase;
        inst.alpha = line.alpha;
    }

    // 普通 alpha 混合:细软蓝线(与雨线的硬边短划区分)
    BlendScope blend(BlendMode::Alpha());

    shader_->Use();
    shader_->SetMat4("uProjection", ctx.projection.Data());
    shader_->SetFloat("uTime", time_);
    // 亮度随流动节奏轻微起伏(线群整体呼吸感);
    // 强度下限抬高(原 0.7 起步,与片元全局系数连乘后过暗)
    const float rhythm = 0.85f + 0.15f * std::sin(time_ * kRhythmFreq);
    shader_->SetFloat("uIntensity", intensity_ * (0.8f + 0.2f * strength_) * rhythm);
    if (flowDir_.y > 0.0f) {
        shader_->SetVec3("uColor", kUpColor[0], kUpColor[1], kUpColor[2]);
    } else if (flowDir_.y < 0.0f) {
        shader_->SetVec3("uColor", kDownColor[0], kDownColor[1], kDownColor[2]);
    } else if (flowDir_.x < 0.0f) {
        shader_->SetVec3("uColor", kLeftColor[0], kLeftColor[1], kLeftColor[2]);
    } else {
        shader_->SetVec3("uColor", kRightColor[0], kRightColor[1], kRightColor[2]);
    }

    UploadInstances(instances_.data(), instances_.size() * sizeof(WindInstance));
    DrawInstanced(instances_.size());
}
