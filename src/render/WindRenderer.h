#pragma once

// =============================================================================
// 风线渲染器(阵风可视化,A4-b 补充):阵风生效期间以细线束描绘气流。
// - 形态(用户需求):细直短线束——比雨线更细(宽 1.3~1.9px,雨线 0.6~2.4px;
//   宽度下限 ~1.3px 是可见性底线:更细会落入亚像素,无 MSAA 下被光栅化稀释)、
//   更密集(48 条同屏,一次性成批生成,长 110~200px 连续长线);
//   持续沿风向整体流动,且速度有节奏地脉动(全局节奏正弦调制,时快时慢如阵风推进)。
// - 风向四向支持:上/下/左/右轴对齐(每次阵风至多一种),线取向与流动方向跟随流向。
// - 与雨线的视觉区分:雨=较粗的硬边短斜线密集下落(蓝白、下坠方向固定);
//   风线=更细的柔核长线(主题蓝)沿风向水平/垂直流动、涡摆缓慢、节奏性推进。
// - 粒子数据在本渲染器内程序化维护(纯表现层,无玩法逻辑);
//   WeatherDirector 每帧喂阵风状态 SetGust(生效?, 推力向量),
//   再经 IRenderer::Update(dt) 统一推进(预警期无气流)。
// - 绘制层级:道具/巨化星之后、蝙蝠之前——气流贴玩法平面流动,蝙蝠保持清晰
//   (认领 WorldSlot::WindLines 槽位)。
// =============================================================================

#include "render/IRenderer.h"
#include "render/InstancedQuadRenderer.h"

#include <memory>
#include <vector>

class Shader;

class WindRenderer : public InstancedQuadRenderer
{
public:
    WindRenderer() = default;
    ~WindRenderer() = default;

    // 需在 GL 上下文就绪、且 "wind" shader 已通过 ShaderLibrary 预编译后调用
    void Init();

    // 每帧喂入阵风状态(WeatherDirector 在统一 Update 之前调用):
    // active=阵风是否生效(预警期不算),force=推力向量(仅一轴非零:
    // 上(0,+)/下(0,-)/左(-,0)/右(+,0),幅值映射强度与流速)
    void SetGust(bool active, const Vec2& force) override { gustActive_ = active; gustForce_ = force; }

    // 统一 Update 入口:推进流向切换、起止淡入淡出与线群流动
    void Update(float dt) override;

    // 实例化绘制认领槽位(整体强度 <= 0 时为空操作)
    void Render(const RenderContext& ctx, WorldSlot slot) override;

    std::vector<WorldSlot> Slots() const override { return { WorldSlot::WindLines }; }

private:
    // CPU 侧细线数据(程序化生成与推进;涡摆/收细由 shader 完成)
    struct WindLine {
        Vec2 pos;             // 线中心(沿流向移动)
        float length = 0.0f;  // 线长(px)
        float width = 0.0f;   // 线宽(px)
        float phase = 0.0f;   // 涡摆初相位
        float speed = 0.0f;   // 流速系数(个体差异)
        float alpha = 0.0f;   // 个体基准透明度
    };

    // GPU 实例属性(与 wind.vert 的 location 1~5 一一对应)
    struct WindInstance {
        Vec2 pos;      // 1: 中心
        Vec2 dir;      // 2: 流向单位向量
        Vec2 size;     // 3: (宽, 长)
        float phase;   // 4: 涡摆相位
        float alpha;   // 5: 个体透明度
    };

    void ReseedLine(WindLine& line, bool anywhere);

    std::shared_ptr<Shader> shader_;
    std::vector<WindInstance> instances_;
    std::vector<WindLine> lines_;

    float time_ = 0.0f;        // 动画时钟(涡摆 + 流动节奏)
    float intensity_ = 0.0f;   // 整体强度(起止淡入淡出,0~1)
    Vec2 flowDir_ = {0.0f, 1.0f}; // 当前流向(轴对齐单位向量,默认上)
    float strength_ = 0.0f;    // 归一化强度(0~1,映射流动速度)

    bool gustActive_ = false;  // 本帧阵风状态(SetGust 喂入,Update 消费)
    Vec2 gustForce_{0.0f, 0.0f};
};
