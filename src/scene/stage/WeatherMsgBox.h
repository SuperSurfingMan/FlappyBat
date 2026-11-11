#pragma once
#include <string>
#include <vector>

// =============================================================================
// 天气事件消息盒（第 5 期从 WeatherDirector 抽出）：左上角"天象预警"列表。
//
// 职责单一：维护一组带槽位的消息行（预警闪烁 -> 常驻状态），并把它们画出来。
// 它不知道阵风、落雷、风暴档位的存在——谁想播报就 Post 一条，谁想收敛就 Remove。
// 槽位约定见 WeatherDirector.h 的 WeatherMsgSlot：场景自留 0~2，怪物从 3 起分段。
//
// 抽出理由：消息盒有独立的状态机（闪烁/常驻/置顶/容量裁剪）与独立的绘制几何，
// 与天气数值逻辑（阵风推力、雨强、天色）变更理由不同，混在 470 行里互相淹没。
// =============================================================================
class WeatherMsgBox {
public:
    static constexpr size_t MsgCap = 4;

    void Reset();

    // 预警闪烁计时：autoSteady 条目到点自转常驻；其余保持闪烁直到所属系统显式转换
    void Tick(float dt);

    // 新增/刷新预警（置顶闪烁）、转常驻、移除
    void Post(int slot, const std::string& text, float flashTime, bool autoSteady);
    void SetSteady(int slot, const std::string& text);
    void Remove(int slot);

    // 槽位查询（落雷轮询用）：不存在返回 false；steady 非空时回填常驻状态
    bool Exists(int slot, bool* steady = nullptr) const;

    bool Empty() const { return msgs_.empty(); }
    size_t Size() const { return msgs_.size(); }
    float Clock() const { return clock_; }

    void Draw() const; // ImGui 前景层（无消息时为空操作）

private:
    struct Msg {
        int id = 0;
        std::string text;
        float warnTimer = 0.0f;
        bool steady = false;
        bool autoSteady = false;
    };

    std::vector<Msg> msgs_; // 最新消息在首位（第一行）
    float clock_ = 0.0f;
};
