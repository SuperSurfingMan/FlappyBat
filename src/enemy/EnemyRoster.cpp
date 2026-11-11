#include "enemy/EnemyDirector.h"
#include "enemy/owlking/OwlKing.h"
#include "gameplay/ModeRegistry.h"
#include <exception>
#include <string>

// =============================================================================
// 内置怪物登记表（docs/怪物系统架构设计.md §9 操作手册）：
// 新增怪物 = 其文件内实现 IEnemy 后在此加一行 Register（注册序即段落优先级），
// MainScene / 导演 / 其余怪物零改动；CMakeLists 另追加该怪物自己的 .cpp 一行。
// =============================================================================
void RegisterBuiltinEnemies(EnemyDirector& director) {
    director.Register(std::make_shared<OwlKing>()); // D14 血月枭王
}

bool ValidateEnemyTunings(std::string* firstError) {
    const auto fail = [firstError](const std::string& msg) {
        if (firstError) *firstError = msg;
        return false;
    };

    // 怪物的调参表按 ModeKey 查；未登记的模式使用明确命名的默认调参，
    // 因此新增游戏模式不会让怪物代码出现越界或漏配。
    // 新增怪物时，在此追加同样的遍历即可（与上面的登记点保持一一对应）。
    auto& registry = ModeRegistry::Instance();
    for (const IGameMode* mode : registry.All()) {
        const OwlKing::Tuning& t = OwlKing::TuningFor(mode->Key());
        if (t.hpBase <= 0 || t.cdScale <= 0.0f) {
            return fail("OwlKing 调参[模式 '" + std::string(mode->Key()) +
                        "'] 数值非法");
        }
    }
    return true;
}
