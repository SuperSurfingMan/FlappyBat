#pragma once
#include "gameplay/ModeDefinition.h"
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// =============================================================================
// 模式注册表：新增模式的唯一登记点。
//
// 注册序有语义：它是 score.dat 分段存档的段索引。新增模式必须追加在末尾
// （Register 天然追加），这样旧档 modeCount 较小时只填前几段，天然向后兼容；
// 魔数 "FBAT3MOD" 保持不变。
//
// 依赖方向：gameplay 层不认识任何场景 / UI / 存档类型。
// =============================================================================
class ModeRegistry {
public:
    static ModeRegistry& Instance();

    // 重复 key 直接抛错，避免两处注册互相覆盖造成存档段错位
    void Register(std::unique_ptr<IGameMode> mode);

    const IGameMode& Get(ModeKey key) const;   // 未登记抛 out_of_range
    const IGameMode& At(std::size_t index) const;
    std::size_t IndexOf(ModeKey key) const;
    bool Contains(ModeKey key) const;
    std::size_t Size() const { return modes_.size(); }

    // 注册序视图：菜单按钮 / 排行榜页签遍历它，数量自动得出
    const std::vector<IGameMode*>& All() const { return order_; }

    // 仅供自测重建注册表（生产代码不要调用）
    void Clear();

private:
    ModeRegistry() = default;
    std::vector<std::unique_ptr<IGameMode>> modes_;
    std::vector<IGameMode*> order_;
    std::unordered_map<std::string, std::size_t> index_;
};

// 内置模式登记表（ModeRoster.cpp）：组合根在进程最早期调用一次
void RegisterBuiltinModes();

// 便捷取参：等价于 ModeRegistry::Instance().Get(key).Params()
const ModeParams& ParamsOf(ModeKey key);
