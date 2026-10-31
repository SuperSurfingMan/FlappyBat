#include "enemy/EnemyDirector.h"
#include "enemy/owlking/OwlKing.h"

// =============================================================================
// 内置怪物登记表（docs/怪物系统架构设计.md §9 操作手册）：
// 新增怪物 = 其文件内实现 IEnemy 后在此加一行 Register（注册序即段落优先级），
// MainScene / 导演 / 其余怪物零改动；CMakeLists 另追加该怪物自己的 .cpp 一行。
// =============================================================================
void RegisterBuiltinEnemies(EnemyDirector& director) {
    director.Register(std::make_shared<OwlKing>()); // D14 血月枭王
}
