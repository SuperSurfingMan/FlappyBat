#pragma once
// =============================================================================
// 调参（设置页「参数调节」栏目直连的实时玩法参数）
//
// 能力对齐 web 移植版的设置页"参数调节"，但取值点按本工程重构后的落位重排，
// 不再让同一份数值散落在场景/子系统的常量里：
//   InitialInvulnSec  MainScene 倒数结束的开局保护（原 GrantInvincible(3.f) 硬编码）
//   GiantInvulnSec    PlayerAbilities 巨化无敌（原 GiantDuration 常量）
//   ItemFreqScale     ItemField 走廊道具生成概率倍率（原 ItemSpawnProbability）
//   OwlHpScale        OwlKing 血量倍率（hpBase 与 400 上限同步缩放）
//   ThunderFreqScale  WeatherDirector 落雷间隔（名义间隔除以此倍率，越大雷越频繁）
//
// 三个设计约束：
//   1. 规则只写一次——范围、量化（整数卡位 / 0.1）与 0..1 归一化映射都在参数表与
//      Quantize 里，设置页只负责把鼠标位置换算成 [0,1]，不复制 min/max/步进；
//      读数格式也在表内，因此界面显示与实际参与运算的值永远同精度。
//   2. 无全局访问点——本类是普通对象，由组合根构造、经 Services 下发，消费点从
//      Init 参数拿到引用（沿用第 5 期"依赖写在构造签名上"的约定）。
//   3. 偏好即存即生效——持久化走统一存档 P 行（读-改-写语义见 app/UnifiedSave.h），
//      与音量/枭王形象批次的存档约定一致。
//
// 新增一个可调参数：参数表加一行（min/max/量化/默认值/标签/格式）+ 枚举加一项
// + 消费点读一次 getter，设置页会自动多出一行，无需改界面代码。
// =============================================================================
#include "app/UnifiedSave.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

class TuningParams {
public:
    // 序号即设置页行序（与参数表一一对应）
    enum Id {
        InitialInvuln = 0, // 开局无敌时长（秒）
        GiantInvuln,       // 巨化星无敌时长（秒）
        ItemFreq,          // 道具生产频率倍率
        OwlHp,             // 枭王血量倍率
        ThunderFreq,       // 雷击频率倍率
        IdCount
    };
    static constexpr int Count = IdCount;

    // 行规格：标签与读数格式随数值规则存放，避免"界面一份、逻辑一份"的两处真相
    struct Spec {
        const char* label;       // 设置页行标签
        const char* valueFormat; // 读数 printf 格式（精度与量化方式一致）
        float min;
        float max;
        bool  integer;           // true = 滑到整数卡住；false = 连续值 0.1 量化
        float defaultValue;
    };

    static const Spec& SpecOf(int id) {
        static const Spec kSpecs[IdCount] = {
            // label            valueFormat  min    max    integer default
            { "初始无敌时间",   "%.0f 秒",    0.f,   120.f, true,   3.f  }, // 原开局保护 3s
            { "巨化星无敌时间", "%.1f 秒",    1.f,   15.f,  false,  5.f  }, // 原 GiantDuration；下限 1s，形态与无敌共用计时器
            { "道具生产频率",   "x%.1f",      0.f,   3.f,   false,  1.f  }, // 原 ItemSpawnProbability
            { "枭王血量倍率",   "x%.1f",      0.5f,  3.f,   false,  1.f  }, // 原 hpBase 无缩放
            { "雷击频率倍率",   "x%.1f",      0.5f,  3.f,   false,  1.f  }, // 原落雷间隔无缩放
        };
        return kSpecs[(id >= 0 && id < IdCount) ? id : 0];
    }

    TuningParams() { Load(); }

    // ===== 消费点读取：语义化命名，调用处不出现魔法下标 =====
    float InitialInvulnSec() const { return values_[InitialInvuln]; }
    float GiantInvulnSec()   const { return values_[GiantInvuln]; }
    float ItemFreqScale()    const { return values_[ItemFreq]; }
    float OwlHpScale()       const { return values_[OwlHp]; }
    float ThunderFreqScale() const { return values_[ThunderFreq]; }

    float Value(int id) const {
        return (id >= 0 && id < IdCount) ? values_[id] : 0.f;
    }

    // ===== 设置页：位置(0..1) <-> 数值（量化与钳位在此统一，UI 不参与规则）=====
    float Normalized(int id) const {
        const Spec& s = SpecOf(id);
        return std::clamp((Value(id) - s.min) / (s.max - s.min), 0.f, 1.f);
    }

    // 拖动即生效并持久化：量化后与当前值相同则既不写盘也不惊动消费点
    void SetNormalized(int id, float t) {
        if (id < 0 || id >= IdCount) return;
        const Spec& s = SpecOf(id);
        const float v = Quantize(s, s.min + std::clamp(t, 0.0f, 1.0f) * (s.max - s.min));
        if (v == values_[id]) return;
        values_[id] = v;
        Save();
    }

    // 读数文本（格式取自参数表，显示精度与量化精度同源）
    static void FormatValue(char* buf, std::size_t size, int id, float v) {
        std::snprintf(buf, size, SpecOf(id).valueFormat, (double)v);
    }

    // ===== 持久化：统一存档 P 行 =====
    // 缺行/行短（旧档或手改）时缺的部分保留默认值；数值一律过一遍量化与钳位，
    // 手改存档也不会把参数推出合法区间。
    void Load() {
        for (int i = 0; i < IdCount; ++i) values_[i] = SpecOf(i).defaultValue;

        for (const auto& line : UnifiedSave::ReadAllLines()) {
            if (!UnifiedSave::LineHasTag(line, {"P"})) continue;
            std::istringstream ss(line.size() > 2 ? line.substr(2) : std::string());
            for (int i = 0; i < IdCount; ++i) {
                float v = 0.0f;
                if (!(ss >> v)) break; // 旧档行短：后续参数保留默认值
                values_[i] = Quantize(SpecOf(i), v);
            }
            return; // 只认第一行 P
        }
    }

    void Save() const {
        std::ostringstream ss;
        ss << "P";
        for (int i = 0; i < IdCount; ++i) ss << ' ' << values_[i];
        UnifiedSave::RewriteSection({"P"}, {ss.str()}); // 只重写 P 行，保留其它系统的数据
    }

private:
    // 量化 + 钳位：min/max 与步进是同一条规则的两种表述，只在这里实现一次
    static float Quantize(const Spec& s, float v) {
        v = std::clamp(v, s.min, s.max);
        return s.integer ? std::round(v) : std::round(v * 10.0f) / 10.0f;
    }

    float values_[IdCount] = {};
};
